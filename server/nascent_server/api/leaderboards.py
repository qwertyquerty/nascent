from flask import Blueprint, request
from sqlalchemy import select
from sqlalchemy.orm import selectinload

from ..extensions import db
from ..models import User
from ..schemas import pagination
from ..serializers import user_summary
from ..services import ranking

blueprint = Blueprint("leaderboards", __name__, url_prefix="/leaderboards")


@blueprint.get("/performance")
def performance():
    limit, offset = pagination(default_limit=50, max_limit=100, max_offset=1_000_000)
    key_count = ranking.check_key_count(request.args.get("key_count", type=int))
    rows, total = ranking.leaderboard_page(offset, limit, key_count)
    users = {user.id: user for user in db.session.scalars(select(User).options(selectinload(User.key_stats)).where(User.id.in_([user_id for _, user_id, _ in rows])))}
    entries = []

    for rank, user_id, score in rows:
        user = users.get(user_id)

        if user is None:
            continue

        stats = user if key_count is None else next((row for row in user.key_stats if row.key_count == key_count), None)
        entries.append(
            {
                "rank": rank,
                "user": user_summary(user),
                "performance": score,
                "accuracy": stats.accuracy if stats else 0.0,
                "ranked_play_count": stats.ranked_play_count if stats else 0,
            }
        )

    return {"key_count": key_count, "total": total, "entries": entries}
