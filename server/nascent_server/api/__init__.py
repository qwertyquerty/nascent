from flask import Blueprint

from . import auth, charts, leaderboards, me, meta, moderation, plays, reports, stats, users

api = Blueprint("api", __name__, url_prefix="/api/v1")

for module in (auth, charts, leaderboards, me, meta, moderation, plays, reports, stats, users):
    api.register_blueprint(module.blueprint)
