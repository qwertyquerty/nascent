from flask import Blueprint
from sqlalchemy import select

from ..errors import ApiError
from ..extensions import db
from ..ids import parse_id
from ..models import REPORT_REASONS, Play, Report, ReportKind, ReportStatus
from ..ratelimit import check_rate_limit
from ..schemas import ReportIn, parse_body
from ..security import auth_required, current_user, require_not_banned
from ..serializers import report_out
from ..services.charts import get_chart_or_404
from .lookup import get_user_or_404

blueprint = Blueprint("reports", __name__, url_prefix="/reports")


def report_target(kind: ReportKind, target: str) -> dict[str, int]:
    if kind == ReportKind.PLAY:
        play_id = parse_id(target)
        play = db.session.get(Play, play_id) if play_id is not None else None

        if play is None:
            raise ApiError(404, "play_not_found", "No such play")

        return {"play_id": play.id, "user_id": play.user_id}

    if kind == ReportKind.PLAYER:
        return {"user_id": get_user_or_404(target).id}

    return {"chart_id": get_chart_or_404(target).id}


@blueprint.post("")
@auth_required
def create_report():
    reporter = current_user()
    require_not_banned(reporter)
    body = parse_body(ReportIn)

    if body.reason not in REPORT_REASONS[body.kind]:
        raise ApiError(400, "invalid_reason", f"Reports about a {body.kind} can't use the reason {body.reason}")

    target = report_target(body.kind, body.target)

    if target.get("user_id") == reporter.id:
        raise ApiError(400, "own_report", "You can't report yourself or your own plays")

    existing = db.session.scalar(
        select(Report).where(
            Report.reporter_id == reporter.id,
            Report.kind == body.kind,
            Report.status == ReportStatus.OPEN,
            *(getattr(Report, column) == value for column, value in target.items()),
        )
    )

    if existing is not None:
        return {"report": report_summary(existing), "duplicate": True}

    check_rate_limit("report", reporter.id)
    report = Report(kind=body.kind, reason=body.reason, details=body.details, reporter_id=reporter.id, **target)
    db.session.add(report)
    db.session.commit()
    return {"report": report_summary(report), "duplicate": False}, 201


def report_summary(report: Report) -> dict:
    body = report_out(report)
    return {name: body[name] for name in ("id", "kind", "reason", "details", "status", "created_at")}
