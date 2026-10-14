from collections import Counter, defaultdict
from datetime import UTC, datetime, time, timedelta

from flask import Blueprint, request
from sqlalchemy import case, func, select
from sqlalchemy.orm import joinedload

from .. import timeutil
from ..extensions import db
from ..models import COUNTED_PLAY_STATUSES, Chart, Play, User
from ..queries import count_rows
from ..schemas import pagination, query_float, query_int
from ..serializers import chart_summary, key_mode_out, play_out, totals_out, user_profile, user_summary
from ..services import ranking
from ..services.calculator import require_rate_key
from ..services.charts import get_chart_or_404
from .lookup import can_see_private, get_user_or_404

blueprint = Blueprint("users", __name__, url_prefix="/users")

SEARCH_LENGTH = 32
ACTIVITY_DAYS = 365
FAVORITE_CHARTS = 6


@blueprint.get("")
def search():
    term = (request.args.get("q") or "").strip().lower()[:SEARCH_LENGTH]
    limit = query_int("limit", 8, 1, 25)

    if not term:
        return {"users": []}

    escaped = term.replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_")
    starts = User.username_key.like(f"{escaped}%", escape="\\")
    order = case((User.username_key == term, 0), (starts, 1), else_=2)
    users = db.session.scalars(
        select(User)
        .where(User.is_banned.is_(False), User.username_key.like(f"%{escaped}%", escape="\\"))
        .order_by(order, User.last_played_at.desc().nulls_last(), User.username_key)
        .limit(limit)
    ).all()
    return {"users": [{**user_summary(user), "last_played_at": timeutil.isoformat(user.last_played_at)} for user in users]}


@blueprint.get("/<reference>")
def profile(reference: str):
    user = get_user_or_404(reference)
    grades: dict[int, dict[str, int]] = defaultdict(dict)
    all_grades: dict[str, int] = defaultdict(int)

    for key_count, grade, count in db.session.execute(
        select(Play.key_count, Play.grade, func.count()).where(Play.user_id == user.id, Play.is_profile_best.is_(True)).group_by(Play.key_count, Play.grade)
    ):
        grades[key_count][grade] = count
        all_grades[grade] += count

    key_modes = []
    peaks = ranking.peak_ranks(user.id)
    today = timeutil.utcnow().date()

    for row in user.key_stats:
        ranked = row.key_count in ranking.ranked_key_counts()
        rank_value = ranking.global_rank(user, row.key_count) if ranked else None
        ranked_players = ranking.ranked_player_count(row.key_count) if ranked else None
        peak = peaks.get(row.key_count)

        if rank_value is not None and (peak is None or rank_value < peak[0]):
            peak = (rank_value, today)

        key_modes.append(key_mode_out(row, rank_value, ranked_players, grades.get(row.key_count, {}), peak if ranked else None))

    return {"user": user_profile(user, totals_out(user, dict(all_grades)), key_modes)}


@blueprint.get("/<reference>/rank")
def rank(reference: str):
    user = get_user_or_404(reference)
    key_count = ranking.check_key_count(request.args.get("key_count", type=int))
    return {
        "user_id": user.id,
        "key_count": key_count,
        "global_rank": ranking.global_rank(user, key_count),
        "performance": ranking.standing(user, key_count)[0],
        "ranked_players": ranking.ranked_player_count(key_count),
    }


@blueprint.get("/<reference>/history")
def history(reference: str):
    user = get_user_or_404(reference)
    key_count = ranking.check_key_count(request.args.get("key_count", type=int))
    return {
        "user_id": user.id,
        "key_count": key_count,
        "performance": [[day.isoformat(), performance] for day, performance in ranking.performance_history(user.id, key_count)],
        "rank": [[day.isoformat(), rank] for day, rank in ranking.rank_history(user.id, key_count)],
    }


@blueprint.get("/<reference>/activity")
def activity(reference: str):
    user = get_user_or_404(reference)
    visible = [Play.user_id == user.id]

    if not can_see_private(user):
        visible.append(Play.status.in_(COUNTED_PLAY_STATUSES))

    if "key_count" in request.args:
        visible.append(Play.key_count == query_int("key_count", 4, 1, 32))

    today = timeutil.utcnow().date()
    first = today - timedelta(days=ACTIVITY_DAYS - 1)
    since = datetime.combine(first, time(), tzinfo=UTC)
    days = Counter(submitted_at.date() for submitted_at in db.session.scalars(select(Play.submitted_at).where(*visible, Play.submitted_at >= since)))

    plays = func.count().label("plays")
    last_played = func.max(Play.submitted_at).label("last_played_at")
    rows = db.session.execute(
        select(Play.chart_id, plays, func.max(Play.accuracy).label("best_accuracy"), last_played)
        .where(*visible)
        .group_by(Play.chart_id)
        .order_by(plays.desc(), last_played.desc(), Play.chart_id.asc())
        .limit(FAVORITE_CHARTS)
    ).all()
    charts = {chart.id: chart for chart in db.session.scalars(select(Chart).where(Chart.id.in_([row.chart_id for row in rows])))}

    return {
        "user_id": user.id,
        "first_day": first.isoformat(),
        "last_day": today.isoformat(),
        "days": [[day.isoformat(), count] for day, count in sorted(days.items())],
        "favorite_charts": [
            {
                "chart": chart_summary(charts[row.chart_id]),
                "plays": row.plays,
                "best_accuracy": row.best_accuracy,
                "last_played_at": timeutil.isoformat(row.last_played_at),
            }
            for row in rows
        ],
    }


@blueprint.get("/<reference>/plays/best")
def best_plays(reference: str):
    user = get_user_or_404(reference)
    limit, offset = pagination(default_limit=50, max_limit=100, max_offset=10_000)
    key_count = ranking.check_key_count(request.args.get("key_count", type=int))
    weights = ranking.current_weights()
    total_weight = sum(weights)

    query = select(Play).options(joinedload(Play.chart)).where(Play.user_id == user.id, Play.is_profile_best.is_(True), Play.key_count == key_count)
    total = count_rows(query)
    plays = db.session.scalars(query.order_by(Play.performance.desc(), Play.id.asc()).offset(offset).limit(limit)).all()

    entries = []

    for index, play in enumerate(plays):
        position = offset + index
        weight = weights[position] / weights[0] if position < len(weights) else 0.0
        contribution = play.performance * weights[position] / total_weight if position < len(weights) else 0.0
        entries.append({"position": position + 1, "weight": weight, "weighted_performance": contribution, "play": play_out(play, include_user=False)})

    return {"user_id": user.id, "key_count": key_count, "total": total, "plays": entries}


@blueprint.get("/<reference>/plays/recent")
def recent_plays(reference: str):
    user = get_user_or_404(reference)
    limit = query_int("limit", 50, 1, 100)
    offset = query_int("offset", 0, 0, 10_000)
    before = request.args.get("before", type=int)

    query = select(Play).options(joinedload(Play.chart)).where(Play.user_id == user.id)

    if not can_see_private(user):
        query = query.where(Play.status.in_(COUNTED_PLAY_STATUSES))

    if "key_count" in request.args:
        query = query.where(Play.key_count == query_int("key_count", 4, 1, 32))

    total = count_rows(query)

    if before is not None:
        query = query.where(Play.id < before)
        offset = 0

    plays = db.session.scalars(query.order_by(Play.id.desc()).offset(offset).limit(limit)).all()
    next_cursor = plays[-1].id if len(plays) == limit else None
    return {"user_id": user.id, "total": total, "plays": [play_out(play, include_user=False) for play in plays], "next_before": next_cursor}


@blueprint.get("/<reference>/charts/<chart_hash>/plays")
def chart_plays(reference: str, chart_hash: str):
    user = get_user_or_404(reference)
    chart = get_chart_or_404(chart_hash)
    limit, offset = pagination(default_limit=50, max_limit=100, max_offset=10_000)

    query = select(Play).where(Play.user_id == user.id, Play.chart_id == chart.id)

    if not can_see_private(user):
        query = query.where(Play.status.in_(COUNTED_PLAY_STATUSES))

    rate = query_float("rate")

    if rate is not None:
        query = query.where(Play.rate_key == require_rate_key(rate))

    plays = db.session.scalars(query.order_by(Play.accuracy.desc(), Play.max_combo.desc(), Play.id.asc()).offset(offset).limit(limit)).all()
    return {"user_id": user.id, "chart_hash": chart.hash, "plays": [play_out(play, include_user=False, include_chart=False) for play in plays]}
