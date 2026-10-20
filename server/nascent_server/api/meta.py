import nascent_core as nc
from flask import Blueprint, current_app
from sqlalchemy import text
from sqlalchemy.exc import SQLAlchemyError

from ..cache import store
from ..extensions import db
from ..services import email_verification, mail
from ..services.calculator import RATE_KEYS, calculator_version

blueprint = Blueprint("meta", __name__)


@blueprint.get("/health")
def health():
    try:
        db.session.execute(text("SELECT 1"))
        database = True
    except SQLAlchemyError:
        database = False

    cache = store().ping()
    healthy = database and cache
    return {"status": "ok" if healthy else "degraded", "database": database, "cache": cache}, 200 if healthy else 503


@blueprint.get("/info")
def info():
    return {
        "rates": [key / 100 for key in RATE_KEYS],
        "replay_format_version": nc.REPLAY_FORMAT_VERSION,
        "chart_hash_format": nc.CHART_HASH_FORMAT,
        "calculator_version": calculator_version(),
        "hit_scores": nc.HIT_SCORES,
        "profile": {"top_plays": current_app.config["PROFILE_TOP_PLAYS"], "decay": current_app.config["PROFILE_DECAY"]},
        "ranked_key_counts": list(current_app.config["RANKED_KEY_COUNTS"]),
        "performance_accuracy_floor": nc.PERFORMANCE_ACCURACY_FLOOR,
        "password_reset": mail.enabled(),
        "email_verification": email_verification.required(),
        "min_client_version": current_app.config["MIN_CLIENT_VERSION"] or None,
    }
