import uuid

import flask_migrate
from alembic.autogenerate import compare_metadata
from alembic.migration import MigrationContext
from sqlalchemy import inspect, text

from nascent_server import create_app
from nascent_server.cli import nascent_cli
from nascent_server.config import TestConfig
from nascent_server.extensions import db


def fresh_app(tmp_path):
    app = create_app(TestConfig, {"STORAGE_PATH": str(tmp_path / "storage"), "REDIS_PREFIX": f"nascent-test:{uuid.uuid4().hex}:"})

    with app.app_context():
        db.drop_all()
        db.session.execute(text("DROP TABLE IF EXISTS alembic_version"))
        db.session.commit()

    return app


def schema_differences():
    with db.engine.connect() as connection:
        return compare_metadata(MigrationContext.configure(connection), db.metadata)


def clean_up(app):
    with app.app_context():
        db.drop_all()
        db.session.execute(text("DROP TABLE IF EXISTS alembic_version"))
        db.session.commit()


def test_migrations_build_the_models_schema(tmp_path):
    app = fresh_app(tmp_path)

    with app.app_context():
        flask_migrate.upgrade()
        assert schema_differences() == []

    clean_up(app)


def test_upgrade_db_adopts_databases_made_before_migrations(tmp_path):
    app = fresh_app(tmp_path)

    with app.app_context():
        db.create_all()
        db.session.execute(text("DROP INDEX ix_plays_chart_performance"))
        db.session.execute(text("ALTER TABLE users ADD COLUMN performance FLOAT NOT NULL DEFAULT 0"))
        db.session.execute(text("CREATE INDEX ix_users_performance ON users (performance, id)"))
        db.session.commit()

    result = app.test_cli_runner().invoke(nascent_cli, ["upgrade-db"])
    assert result.exit_code == 0, result.output
    assert "marked the existing database" in result.output

    with app.app_context():
        assert "performance" not in {column["name"] for column in inspect(db.engine).get_columns("users")}
        assert schema_differences() == []

    again = app.test_cli_runner().invoke(nascent_cli, ["upgrade-db"])
    assert again.exit_code == 0, again.output
    assert "marked the existing database" not in again.output

    clean_up(app)
