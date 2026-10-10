from flask import Blueprint

from .. import timeutil
from ..schemas import SubmissionIn, TicketIn, parse_body
from ..security import current_user
from ..serializers import chart_detail, play_out
from ..services.plays import Standing, issue_ticket, submit_play
from ..storage import json_gz_response
from .lookup import can_see_private, get_visible_play_or_404

blueprint = Blueprint("plays", __name__, url_prefix="/plays")


def standing_out(standing: Standing) -> dict:
    return {
        "key_count": standing.key_count,
        "performance": standing.performance,
        "performance_change": standing.performance - standing.previous_performance,
        "global_rank": standing.rank,
        "previous_global_rank": standing.previous_rank,
    }


@blueprint.post("/tickets")
def create_ticket():
    user = current_user()
    body = parse_body(TicketIn)
    issued = issue_ticket(user, body.chart_hash, body.rate)
    return {
        "ticket": issued.token,
        "chart_hash": issued.ticket.chart_hash,
        "rate": issued.ticket.rate_key / 100,
        "expires_at": timeutil.isoformat(issued.ticket.expires_at),
        "chart_known": issued.chart is not None,
        "chart_status": issued.chart.status if issued.chart is not None else None,
    }, 201


@blueprint.post("")
def submit():
    user = current_user()
    body = parse_body(SubmissionIn)
    result = submit_play(user, body.chart_hash, body.replay, body.chart, body.ticket)
    return {
        "play": play_out(result.play, include_chart=False, include_flags=True),
        "chart": chart_detail(result.chart),
        "chart_created": result.chart_created,
        "chart_rank": result.chart_rank,
        "user": standing_out(result.combined),
        "key_mode": standing_out(result.key_mode) if result.key_mode is not None else None,
    }, 201


@blueprint.get("/<int:play_id>")
def get_play(play_id: int):
    play = get_visible_play_or_404(play_id)
    return {"play": play_out(play, include_flags=can_see_private(play.user))}


@blueprint.get("/<int:play_id>/replay")
def get_replay(play_id: int):
    play = get_visible_play_or_404(play_id)
    return json_gz_response(play.replay_key, f"nascent-replay-{play.id}.json")
