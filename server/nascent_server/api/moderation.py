from flask import Blueprint, request
from sqlalchemy import select
from sqlalchemy.orm import joinedload

from .. import tasks
from ..errors import ApiError
from ..extensions import db
from ..models import Play, PlayStatus, Role
from ..schemas import ChartStatusIn, RestrictIn, ReviewIn, RoleIn, pagination, parse_body
from ..security import current_user, role_required
from ..serializers import chart_detail, play_out, private_user
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


@blueprint.post("/plays/<int:play_id>/review")
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
