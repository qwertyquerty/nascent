from collections import Counter
from typing import Any

import redis
from flask import current_app
from sqlalchemy import case, delete, select, update

from .. import timeutil
from ..cache import store
from ..extensions import db
from ..models import AuthSession, Chart, EmailVerification, PasswordReset, Play, PlayTicket, RankSnapshot, User, UserKeyStats
from ..serializers import play_out, private_user
from ..storage import sharded_key, storage
from . import ranking

OWNED_BY_USER = (Play, PlayTicket, AuthSession, PasswordReset, EmailVerification, UserKeyStats, RankSnapshot)


def export_account(user: User) -> dict[str, Any]:
    sessions = db.session.scalars(select(AuthSession).where(AuthSession.user_id == user.id).order_by(AuthSession.created_at))
    plays = db.session.scalars(select(Play).where(Play.user_id == user.id).order_by(Play.id))
    return {
        "exported_at": timeutil.isoformat(timeutil.utcnow()),
        "user": {**private_user(user), "last_played_at": timeutil.isoformat(user.last_played_at)},
        "key_modes": [{"key_count": row.key_count, "performance": row.performance, "accuracy": row.accuracy, "play_count": row.play_count} for row in user.key_stats],
        "sessions": [
            {
                "client": row.client,
                "ip": row.ip,
                "created_at": timeutil.isoformat(row.created_at),
                "last_used_at": timeutil.isoformat(row.last_used_at),
                "revoked_at": timeutil.isoformat(row.revoked_at) if row.revoked_at else None,
            }
            for row in sessions
        ],
        "plays": [{**play_out(play, include_private=True), "settings": play.settings, "replay": f"/api/v1/plays/{play.id}/replay"} for play in plays],
    }


def delete_account(user: User) -> None:
    plays = db.session.execute(select(Play.chart_id, Play.replay_key, Play.replay_hash).where(Play.user_id == user.id)).all()
    revision = current_app.config["JUDGE_REVISION"]
    files = [row.replay_key for row in plays] + [sharded_key("timelines", row.replay_hash, f".j{revision}.json.gz") for row in plays]

    for chart_id, count in Counter(row.chart_id for row in plays).items():
        db.session.execute(update(Chart).where(Chart.id == chart_id).values(play_count=case((Chart.play_count > count, Chart.play_count - count), else_=0)))

    db.session.execute(update(Chart).where(Chart.uploaded_by_id == user.id).values(uploaded_by_id=None))
    db.session.execute(update(Chart).where(Chart.ranked_by_id == user.id).values(ranked_by_id=None))
    db.session.execute(update(Play).where(Play.reviewed_by_id == user.id).values(reviewed_by_id=None))

    for model in OWNED_BY_USER:
        db.session.execute(delete(model).where(model.user_id == user.id))

    user_id = user.id
    db.session.delete(user)
    db.session.commit()

    try:
        store().leaderboard_apply([(ranking.board_name(key_count), user_id, None) for key_count in ranking.ranked_key_counts()])
    except redis.RedisError:
        current_app.logger.warning("could not remove deleted user %s from the leaderboards", user_id)

    for key in files:
        try:
            storage().delete(key)
        except OSError:
            current_app.logger.warning("could not delete %s for deleted user %s", key, user_id)
