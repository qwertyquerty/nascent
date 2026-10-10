import os

from flask import Flask, request
from werkzeug.middleware.proxy_fix import ProxyFix

from .cache import init_store
from .config import Config
from .errors import register_error_handlers
from .extensions import db, migrate
from .storage import init_storage

MIGRATIONS_DIRECTORY = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "migrations")


def _apply_cors(app: Flask) -> None:
    @app.after_request
    def cors(response):
        origin = request.headers.get("Origin")
        allowed = app.config["CORS_ORIGINS"]

        if origin and (origin in allowed or "*" in allowed):
            response.headers["Access-Control-Allow-Origin"] = origin
            response.headers["Access-Control-Allow-Headers"] = "Authorization, Content-Type, Content-Encoding"
            response.headers["Access-Control-Allow-Methods"] = "GET, POST, PUT, DELETE, OPTIONS"
            response.headers["Access-Control-Expose-Headers"] = "Retry-After"
            response.headers["Access-Control-Max-Age"] = "600"
            response.vary.add("Origin")

        return response


def create_app(config: type[Config] | None = None, overrides: dict | None = None) -> Flask:
    app = Flask(__name__)
    app.config.from_object(config or Config)
    app.config.update(overrides or {})
    app.json.sort_keys = False

    if app.config["TRUST_PROXY_HOPS"]:
        hops = app.config["TRUST_PROXY_HOPS"]
        app.wsgi_app = ProxyFix(app.wsgi_app, x_for=hops, x_proto=hops, x_host=hops)

    db.init_app(app)
    migrate.init_app(app, db, directory=MIGRATIONS_DIRECTORY)
    init_store(app)
    init_storage(app)

    if not app.config.get("TESTING") and not app.config.get("REDIS_URL"):
        app.logger.warning("NASCENT_REDIS_URL is not set, rate limits and the global leaderboard are kept per process")
    register_error_handlers(app)
    _apply_cors(app)

    from .api import api
    from .cli import nascent_cli

    app.register_blueprint(api)
    app.cli.add_command(nascent_cli)
    return app
