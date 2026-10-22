from flask import Blueprint, request
from sqlalchemy import func, select, update
from sqlalchemy.orm import joinedload

from .. import tasks, timeutil
from ..errors import ApiError
from ..extensions import db
from ..models import Play, PlayStatus, Report, ReportStatus, Role
from ..schemas import ChartStatusIn, ReportResolveIn, RestrictIn, ReviewIn, RoleIn, pagination, parse_body
from ..security import current_user, role_required
from ..serializers import chart_detail, play_out, private_user, report_out
from ..services import ranking
from ..services.charts import get_chart_or_404, set_chart_status
from ..services.plays import review_play
from .lookup import get_user_or_404

blueprint = Blueprint("moderation", __name__, url_prefix="/moderation")


@blueprint.get("/plays")
@role_required(Role.MODERATOR)
def review_queue():
    limit, offset = pagination()
    status = request.args.get("status", PlayStatus.PENDING_REVIEW)

    if status not in {item.value for item in PlayStatus}:
        raise ApiError(400, "invalid_query", "Unknown play status")

    plays = db.session.scalars(
        select(Play).options(joinedload(Play.user), joinedload(Play.chart)).where(Play.status == status).order_by(Play.id.asc()).offset(offset).limit(limit)
    ).all()
    return {"plays": [play_out(play, include_review=True) for play in plays]}


@blueprint.post("/plays/<id:play_id>/review")
@role_required(Role.MODERATOR)
def review(play_id: int):
    body = parse_body(ReviewIn)
    play = db.session.get(Play, play_id)

    if play is None:
        raise ApiError(404, "play_not_found", "No such play")

    review_play(play, current_user(), body.decision == "approve", body.note)
    db.session.commit()
    ranking.sync_leaderboard(play.user)
    return {"play": play_out(play, include_review=True)}


@blueprint.put("/charts/<chart_hash>/status")
@role_required(Role.ADMIN)
def chart_status(chart_hash: str):
    body = parse_body(ChartStatusIn)
    chart = get_chart_or_404(chart_hash)
    changed = set_chart_status(chart, body.status, current_user())
    db.session.commit()
    job = tasks.enqueue(tasks.apply_chart_status, chart.id) if changed else None
    return {"chart": chart_detail(chart), "changed": changed, "job_id": job}


@blueprint.put("/users/<reference>/restriction")
@role_required(Role.MODERATOR)
def restrict(reference: str):
    body = parse_body(RestrictIn)
    user = get_user_or_404(reference)

    if user.has_role(Role.MODERATOR) and not current_user().has_role(Role.ADMIN):
        raise ApiError(403, "forbidden", "Only admins can restrict staff")

    user.is_banned = body.restricted
    db.session.commit()
    ranking.sync_leaderboard(user)
    return {"user": private_user(user)}


@blueprint.put("/users/<reference>/role")
@role_required(Role.ADMIN)
def set_role(reference: str):
    body = parse_body(RoleIn)
    user = get_user_or_404(reference)
    user.role = body.role
    db.session.commit()
    return {"user": private_user(user)}


@blueprint.get("/reports")
@role_required(Role.MODERATOR)
def report_queue():
    limit, offset = pagination()
    status = request.args.get("status", ReportStatus.OPEN)

    if status not in {item.value for item in ReportStatus}:
        raise ApiError(400, "invalid_query", "Unknown report status")

    order = Report.id.asc() if status == ReportStatus.OPEN else Report.id.desc()
    reports = db.session.scalars(
        select(Report)
        .options(
            joinedload(Report.reporter),
            joinedload(Report.user),
            joinedload(Report.play).joinedload(Play.user),
            joinedload(Report.play).joinedload(Play.chart),
            joinedload(Report.chart),
            joinedload(Report.resolved_by),
        )
        .where(Report.status == status)
        .order_by(order)
        .offset(offset)
        .limit(limit)
    ).all()
    waiting = db.session.scalar(select(func.count()).select_from(Report).where(Report.status == ReportStatus.OPEN)) or 0
    return {"reports": [report_out(report) for report in reports], "open": waiting}


@blueprint.post("/reports/<id:report_id>/resolve")
@role_required(Role.MODERATOR)
def resolve_report(report_id: int):
    body = parse_body(ReportResolveIn)
    report = db.session.get(Report, report_id)

    if report is None:
        raise ApiError(404, "report_not_found", "No such report")

    if report.status != ReportStatus.OPEN:
        raise ApiError(409, "report_closed", "This report was already closed")

    same_target = [Report.kind == report.kind, Report.status == ReportStatus.OPEN]

    for column in ("play_id", "user_id", "chart_id"):
        value = getattr(report, column)
        same_target.append(getattr(Report, column) == value if value is not None else getattr(Report, column).is_(None))

    closed = db.session.execute(
        update(Report)
        .where(*same_target)
        .values(status=body.decision, resolved_by_id=current_user().id, resolved_at=timeutil.utcnow(), resolution_note=body.note)
        .execution_options(synchronize_session="fetch")
    ).rowcount
    db.session.commit()
    db.session.refresh(report)
    return {"report": report_out(report), "closed": closed}
