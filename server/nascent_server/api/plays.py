import json

import nascent_core as nc
from flask import Blueprint, current_app, request
from sqlalchemy import select
from sqlalchemy.orm import joinedload

from .. import timeutil
from ..errors import ApiError
from ..extensions import db
from ..models import COUNTED_PLAY_STATUSES, Play, User
from ..queries import cached_count
from ..ratelimit import check_rate_limit
from ..schemas import SubmissionIn, TicketIn, pagination, parse_body, query_int
from ..security import current_user, require_not_banned
from ..serializers import chart_detail, play_out
from ..services.charts import native_chart
from ..services.plays import Standing, chart_board_size, chart_rank, issue_ticket, submit_play
from ..storage import get_json_gz, json_gz_response, put_json_gz, sharded_key, storage, stored_json_response
from .lookup import can_see_private, get_visible_play_or_404, viewer_is_moderator

blueprint = Blueprint("plays", __name__, url_prefix="/plays")

SORTS = ("recent", "performance")
TIMELINE_MAX_AGE = 3600


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


@blueprint.get("")
def list_plays():
    limit, offset = pagination(default_limit=50, max_limit=100, max_offset=10_000)
    sort = request.args.get("sort", "recent")

    if sort not in SORTS:
        raise ApiError(400, "invalid_query", f"sort must be one of {', '.join(SORTS)}")

    query = select(Play).join(User, User.id == Play.user_id).where(User.is_banned.is_(False), Play.status.in_(COUNTED_PLAY_STATUSES))

    if "key_count" in request.args:
        query = query.where(Play.key_count == query_int("key_count", 4, 1, 32))

    if sort == "performance":
        query = query.where(Play.ranked.is_(True), Play.eligible.is_(True))
        order = (Play.performance.desc(), Play.id.asc())
    else:
        order = (Play.id.desc(),)

    total = cached_count(query)
    plays = db.session.scalars(query.options(joinedload(Play.user), joinedload(Play.chart)).order_by(*order).offset(offset).limit(limit)).all()
    return {"sort": sort, "total": total, "plays": [play_out(play) for play in plays]}


@blueprint.post("")
def submit():
    user = current_user()
    require_not_banned(user)
    check_rate_limit("submit_burst", user.id)
    check_rate_limit("submit", user.id)
    body = parse_body(SubmissionIn, large=True)
    result = submit_play(user, body.chart_hash, body.replay, body.chart, body.ticket)
    return {
        "play": play_out(result.play, include_chart=False, include_private=True),
        "chart": chart_detail(result.chart),
        "chart_created": result.chart_created,
        "chart_rank": result.chart_rank,
        "key_mode": standing_out(result.key_mode) if result.key_mode is not None else None,
        "previous_best_performance": result.previous_best_performance,
    }, 201


@blueprint.get("/<id:play_id>")
def get_play(play_id: int):
    play = get_visible_play_or_404(play_id)
    rank = chart_rank(play) if not play.user.is_banned else None
    leaderboard = {"rank": rank, "total": chart_board_size(play.chart_id, play.rate_key), "rate": play.rate} if rank is not None else None
    return {"play": play_out(play, include_private=can_see_private(play.user), include_review=viewer_is_moderator()), "leaderboard": leaderboard}


@blueprint.get("/<id:play_id>/replay")
def get_replay(play_id: int):
    play = get_visible_play_or_404(play_id)
    return json_gz_response(play.replay_key, f"nascent-replay-{play.id}.json")


def build_timeline(play: Play) -> dict:
    native = native_chart(play.chart)
    replay = nc.Replay.from_json(get_json_gz(play.replay_key))
    simulation = nc.simulate(native, replay)
    notes = [
        {
            "lane": note.lane,
            "hold": note.is_hold,
            "time": note.time,
            "end_time": note.end_time,
            "hit_played": note.hit_played,
            "hit_error": note.hit_error,
            "hit_score": note.hit_score,
            "release_played": note.release_played,
            "release_error": note.release_error,
            "release_score": note.release_score,
        }
        for note in simulation.notes
    ]
    return {
        "play_id": play.id,
        "rate": replay.rate,
        "key_count": native.key_count,
        "end_time": replay.end_time,
        "scroll_speed": replay.settings.get("scroll_speed"),
        "inputs": [[time, lane, pressed] for time, lane, pressed in replay.inputs],
        "notes": notes,
    }


def timeline_key(play: Play) -> str:
    return sharded_key("timelines", play.replay_hash, f".j{current_app.config['JUDGE_REVISION']}.json.gz")


@blueprint.get("/<id:play_id>/timeline")
def get_timeline(play_id: int):
    play = get_visible_play_or_404(play_id)
    key = timeline_key(play)

    if not storage().exists(key):
        put_json_gz(key, json.dumps(build_timeline(play), separators=(",", ":")))

    return stored_json_response(key, TIMELINE_MAX_AGE)
