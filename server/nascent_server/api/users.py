from collections import defaultdict

from flask import Blueprint, request
from sqlalchemy import func, select
from sqlalchemy.orm import joinedload

from ..extensions import db
from ..models import Play, PlayStatus
from ..schemas import pagination, query_float, query_int
from ..serializers import play_out, stats_out, user_profile
from ..services import ranking
from ..services.calculator import require_rate_key
from ..services.charts import get_chart_or_404
from .lookup import can_see_private, get_user_or_404

blueprint = Blueprint("users", __name__, url_prefix="/users")


@blueprint.get("/<reference>")
def profile(reference: str):
    user = get_user_or_404(reference)
    grades: dict[int, dict[str, int]] = defaultdict(dict)
    combined_grades: dict[str, int] = defaultdict(int)

    for key_count, grade, count in db.session.execute(
        select(Play.key_count, Play.grade, func.count()).where(Play.user_id == user.id, Play.is_profile_best.is_(True)).group_by(Play.key_count, Play.grade)
    ):
        grades[key_count][grade] = count
        combined_grades[grade] += count

    key_modes = []

    for row in user.key_stats:
        rank_value = ranking.global_rank(user, row.key_count) if row.key_count in ranking.ranked_key_counts() else None
        key_modes.append({"key_count": row.key_count, **stats_out(row, rank_value, grades.get(row.key_count, {}))})

    return {"user": user_profile(user, stats_out(user, ranking.global_rank(user), dict(combined_grades)), key_modes)}


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


@blueprint.get("/<reference>/plays/best")
def best_plays(reference: str):
    user = get_user_or_404(reference)
    limit, offset = pagination(default_limit=50, max_limit=100, max_offset=10_000)
    key_count = ranking.check_key_count(request.args.get("key_count", type=int))
    weights = ranking.current_weights()
    total_weight = sum(weights)

    query = select(Play).options(joinedload(Play.chart)).where(Play.user_id == user.id, Play.is_profile_best.is_(True))

    if key_count is not None:
        query = query.where(Play.key_count == key_count)

    plays = db.session.scalars(query.order_by(Play.performance.desc(), Play.id.asc()).offset(offset).limit(limit)).all()

    entries = []

    for index, play in enumerate(plays):
        position = offset + index
        weight = weights[position] / weights[0] if position < len(weights) else 0.0
        contribution = play.performance * weights[position] / total_weight if position < len(weights) else 0.0
        entries.append({"position": position + 1, "weight": weight, "weighted_performance": contribution, "play": play_out(play, include_user=False)})

    return {"user_id": user.id, "key_count": key_count, "plays": entries}


@blueprint.get("/<reference>/plays/recent")
def recent_plays(reference: str):
    user = get_user_or_404(reference)
    limit = query_int("limit", 50, 1, 100)
    before = request.args.get("before", type=int)

    query = select(Play).options(joinedload(Play.chart)).where(Play.user_id == user.id)

    if not can_see_private(user):
        query = query.where(Play.status == PlayStatus.VERIFIED)

    if before is not None:
        query = query.where(Play.id < before)

    plays = db.session.scalars(query.order_by(Play.id.desc()).limit(limit)).all()
    next_cursor = plays[-1].id if len(plays) == limit else None
    return {"user_id": user.id, "plays": [play_out(play, include_user=False) for play in plays], "next_before": next_cursor}


@blueprint.get("/<reference>/charts/<chart_hash>/plays")
def chart_plays(reference: str, chart_hash: str):
    user = get_user_or_404(reference)
    chart = get_chart_or_404(chart_hash)
    limit, offset = pagination(default_limit=50, max_limit=100, max_offset=10_000)

    query = select(Play).where(Play.user_id == user.id, Play.chart_id == chart.id)

    if not can_see_private(user):
        query = query.where(Play.status == PlayStatus.VERIFIED)

    rate = query_float("rate")

    if rate is not None:
        query = query.where(Play.rate_key == require_rate_key(rate))

    plays = db.session.scalars(query.order_by(Play.accuracy.desc(), Play.max_combo.desc(), Play.id.asc()).offset(offset).limit(limit)).all()
    return {"user_id": user.id, "chart_hash": chart.hash, "plays": [play_out(play, include_user=False, include_chart=False) for play in plays]}
