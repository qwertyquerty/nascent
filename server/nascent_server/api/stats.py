import threading
import time
from collections.abc import Callable
from datetime import UTC, datetime, timedelta
from datetime import time as day_start

from flask import Blueprint, current_app
from sqlalchemy import func, select
from sqlalchemy.orm import joinedload

from .. import timeutil
from ..extensions import db
from ..models import COUNTED_PLAY_STATUSES, Chart, ChartStatus, Play, User, UserKeyStats
from ..serializers import chart_summary, play_out
from ..services import ranking

blueprint = Blueprint("stats", __name__, url_prefix="/stats")

TOP_PLAYS = 10
ACTIVITY_DAYS = 90
WEEK_DAYS = 7
POPULAR_CHARTS = 10

_caches: dict[str, dict] = {}
_cache_lock = threading.Lock()


def _cached(name: str, seconds: int, compute: Callable[[], dict]):
    with _cache_lock:
        entry = _caches.setdefault(name, {"at": 0.0, "body": None})

        if seconds > 0 and entry["body"] is not None and time.monotonic() - entry["at"] < seconds:
            body = entry["body"]
        else:
            body = compute()
            entry.update(at=time.monotonic(), body=body)

    return body, 200, {"Cache-Control": f"public, max-age={seconds}"} if seconds > 0 else {}


@blueprint.get("")
def stats():
    return _cached("stats", current_app.config["STATS_CACHE_SECONDS"], _compute_stats)


@blueprint.get("/activity")
def activity():
    return _cached("activity", current_app.config["STATS_ACTIVITY_CACHE_SECONDS"], _compute_activity)


def _counted_plays():
    return select(Play).join(User, User.id == Play.user_id).where(User.is_banned.is_(False), Play.status.in_(COUNTED_PLAY_STATUSES))


def _compute_activity() -> dict:
    today = timeutil.utcnow().date()
    first = today - timedelta(days=ACTIVITY_DAYS - 1)
    since = datetime.combine(first, day_start(), tzinfo=UTC)
    week = timeutil.utcnow() - timedelta(days=WEEK_DAYS)
    counted = _counted_plays().subquery()

    day = func.date(counted.c.submitted_at)
    per_day = {
        str(row[0])[:10]: row[1]
        for row in db.session.execute(select(day, func.count()).where(counted.c.submitted_at >= since).group_by(day))
    }
    days = [first + timedelta(days=offset) for offset in range(ACTIVITY_DAYS)]

    plays_week, active_week = db.session.execute(
        select(func.count(), func.count(func.distinct(counted.c.user_id))).where(counted.c.submitted_at >= week)
    ).one()

    plays = func.count().label("plays")
    players = func.count(func.distinct(counted.c.user_id)).label("players")
    popular = db.session.execute(
        select(counted.c.chart_id, plays, players)
        .where(counted.c.submitted_at >= week)
        .group_by(counted.c.chart_id)
        .order_by(plays.desc(), players.desc(), counted.c.chart_id.asc())
        .limit(POPULAR_CHARTS)
    ).all()
    charts = {chart.id: chart for chart in db.session.scalars(select(Chart).where(Chart.id.in_([row.chart_id for row in popular])))}

    return {
        "first_day": first.isoformat(),
        "last_day": today.isoformat(),
        "plays_per_day": [[value.isoformat(), per_day.get(value.isoformat(), 0)] for value in days],
        "week": {
            "plays": plays_week,
            "active_players": active_week,
            "new_players": db.session.scalar(select(func.count()).select_from(User).where(User.is_banned.is_(False), User.created_at >= week)) or 0,
        },
        "popular_charts": [{"chart": chart_summary(charts[row.chart_id]), "plays": row.plays, "players": row.players} for row in popular],
        "key_modes": [
            {"key_count": key_count, "plays": count}
            for key_count, count in db.session.execute(select(counted.c.key_count, func.count()).group_by(counted.c.key_count).order_by(counted.c.key_count))
        ],
        "grades": dict(db.session.execute(select(counted.c.grade, func.count()).where(counted.c.ranked.is_(True)).group_by(counted.c.grade)).all()),
    }


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
