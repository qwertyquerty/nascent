from flask import Blueprint
from sqlalchemy import func, select
from sqlalchemy.orm import joinedload

from ..extensions import db
from ..models import Chart, ChartStatus, Play, PlayStatus, User, UserKeyStats
from ..serializers import play_out
from ..services import ranking

blueprint = Blueprint("stats", __name__, url_prefix="/stats")

TOP_PLAYS = 10


@blueprint.get("")
def stats():
    visible_user = User.is_banned.is_(False)
    visible_plays = select(func.count()).select_from(Play).join(User, User.id == Play.user_id).where(visible_user, Play.status == PlayStatus.VERIFIED)

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
        "plays": db.session.scalar(visible_plays) or 0,
        "ranked_plays": db.session.scalar(visible_plays.where(Play.ranked.is_(True))) or 0,
        "charts": db.session.scalar(select(func.count()).select_from(Chart)) or 0,
        "ranked_charts": db.session.scalar(select(func.count()).select_from(Chart).where(Chart.status == ChartStatus.RANKED)) or 0,
        "total_playtime_ms": db.session.scalar(select(func.coalesce(func.sum(User.total_playtime_ms), 0)).where(visible_user)) or 0,
        "top_plays": [play_out(play) for play in top_plays],
    }
