import threading
import time

from flask import Blueprint, current_app
from sqlalchemy import func, select
from sqlalchemy.orm import joinedload

from ..extensions import db
from ..models import COUNTED_PLAY_STATUSES, Chart, ChartStatus, Play, User, UserKeyStats
from ..serializers import play_out
from ..services import ranking

blueprint = Blueprint("stats", __name__, url_prefix="/stats")

TOP_PLAYS = 10

_cache: dict = {"at": 0.0, "body": None}
_cache_lock = threading.Lock()


@blueprint.get("")
def stats():
    seconds = current_app.config["STATS_CACHE_SECONDS"]

    with _cache_lock:
        if seconds > 0 and _cache["body"] is not None and time.monotonic() - _cache["at"] < seconds:
            body = _cache["body"]
        else:
            body = _compute_stats()
            _cache.update(at=time.monotonic(), body=body)

    return body, 200, {"Cache-Control": f"public, max-age={seconds}"} if seconds > 0 else {}


def _compute_stats() -> dict:
    visible_user = User.is_banned.is_(False)
    plays, ranked_plays = db.session.execute(
        select(func.count(), func.count().filter(Play.ranked.is_(True)))
        .select_from(Play)
        .join(User, User.id == Play.user_id)
        .where(visible_user, Play.status.in_(COUNTED_PLAY_STATUSES))
    ).one()
    charts, ranked_charts = db.session.execute(select(func.count(), func.count().filter(Chart.status == ChartStatus.RANKED)).select_from(Chart)).one()

    top_plays = db.session.scalars(
        select(Play)
        .join(User, User.id == Play.user_id)
        .options(joinedload(Play.user), joinedload(Play.chart))
        .where(visible_user, Play.ranked.is_(True), Play.is_profile_best.is_(True))
        .order_by(Play.performance.desc(), Play.id.asc())
        .limit(TOP_PLAYS)
    ).all()

    return {
        "players": db.session.scalar(select(func.count()).select_from(User).where(visible_user)) or 0,
        "ranked_players": db.session.scalar(
            select(func.count(func.distinct(UserKeyStats.user_id)))
            .join(User, User.id == UserKeyStats.user_id)
            .where(visible_user, UserKeyStats.ranked_play_count > 0, UserKeyStats.key_count.in_(ranking.ranked_key_counts()))
        )
        or 0,
        "plays": plays,
        "ranked_plays": ranked_plays,
        "charts": charts,
        "ranked_charts": ranked_charts,
        "total_playtime_ms": int(db.session.scalar(select(func.coalesce(func.sum(User.total_playtime_ms), 0)).where(visible_user)) or 0),
        "top_plays": [play_out(play) for play in top_plays],
    }
