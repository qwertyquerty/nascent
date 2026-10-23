from flask import Blueprint, request
from sqlalchemy import select
from sqlalchemy.orm import selectinload

from ..errors import ApiError
from ..extensions import db
from ..models import User, UserKeyStats
from ..queries import cached_count
from ..schemas import pagination
from ..serializers import user_summary
from ..services import ranking

blueprint = Blueprint("leaderboards", __name__, url_prefix="/leaderboards")

SORTS = {
    "performance": UserKeyStats.performance,
    "accuracy": UserKeyStats.accuracy,
    "ranked_plays": UserKeyStats.ranked_play_count,
}


def sorted_page(offset: int, limit: int, key_count: int, sort: str) -> tuple[list[tuple[int, int, float]], int]:
    query = (
        select(UserKeyStats.user_id, UserKeyStats.performance)
        .join(User, User.id == UserKeyStats.user_id)
        .where(UserKeyStats.key_count == key_count, UserKeyStats.ranked_play_count > 0, User.is_banned.is_(False))
    )
    total = cached_count(query)
    rows = db.session.execute(
        query.order_by(SORTS[sort].desc(), UserKeyStats.performance.desc(), UserKeyStats.user_id.asc()).offset(offset).limit(limit)
    ).all()
    return [(offset + index + 1, row.user_id, row.performance) for index, row in enumerate(rows)], total


@blueprint.get("/performance")
def performance():
    limit, offset = pagination(default_limit=50, max_limit=100, max_offset=1_000_000)
    key_count = ranking.check_key_count(request.args.get("key_count", type=int))
    sort = request.args.get("sort", "performance")

    if sort not in SORTS:
        raise ApiError(400, "invalid_query", f"sort must be one of {', '.join(SORTS)}")

    rows, total = ranking.leaderboard_page(offset, limit, key_count) if sort == "performance" else sorted_page(offset, limit, key_count, sort)
    users = {user.id: user for user in db.session.scalars(select(User).options(selectinload(User.key_stats)).where(User.id.in_([user_id for _, user_id, _ in rows])))}
    entries = []

    for rank, user_id, score in rows:
        user = users.get(user_id)

        if user is None:
            continue

        stats = next((row for row in user.key_stats if row.key_count == key_count), None)
        entries.append(
            {
                "rank": rank,
                "user": user_summary(user),
                "performance": score,
                "accuracy": stats.accuracy if stats else 0.0,
                "ranked_play_count": stats.ranked_play_count if stats else 0,
            }
        )

    return {"key_count": key_count, "sort": sort, "total": total, "entries": entries}
