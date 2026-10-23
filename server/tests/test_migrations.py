import uuid

import flask_migrate
from alembic.autogenerate import compare_metadata
from alembic.migration import MigrationContext
from sqlalchemy import inspect, text

from nascent_server import create_app
from nascent_server.cli import nascent_cli
from nascent_server.config import TestConfig
from nascent_server.extensions import db
from nascent_server.models import in_dialect


def fresh_app(tmp_path):
    app = create_app(TestConfig, {"STORAGE_PATH": str(tmp_path / "storage"), "REDIS_PREFIX": f"nascent-test:{uuid.uuid4().hex}:"})

    with app.app_context():
        db.drop_all()
        db.session.execute(text("DROP TABLE IF EXISTS alembic_version"))
        db.session.commit()

    return app


def schema_differences():
    with db.engine.connect() as connection:
        return compare_metadata(MigrationContext.configure(connection, opts={"include_object": in_dialect(connection.dialect.name)}), db.metadata)


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


def test_downloadable_charts_are_backfilled_from_their_sources(tmp_path):
    app = fresh_app(tmp_path)
    chart = {
        "status": "ranked", "key_count": 4, "hit_accuracy": 5, "damage": 5, "title": "Song", "title_unicode": "Song", "artist": "Artist",
        "artist_unicode": "Artist", "mapper": "Mapper", "difficulty_name": "Hard", "note_count": 100, "hold_count": 0, "duration_ms": 60000,
        "difficulty": 1.0, "calculator_version": "1", "play_count": 0, "created_at": "2026-10-09 12:00:00",
    }
    sources = {1: ("osu", 7), 2: ("osu", None), 3: ("etterna", 42), 4: ("other", 9)}

    with app.app_context():
        flask_migrate.upgrade(revision="0010")

        for chart_id in (*sources, 5):
            columns = {"id": chart_id, "hash": f"{chart_id:064x}", "data_key": f"charts/{chart_id}", **chart}
            db.session.execute(text(f"INSERT INTO charts ({', '.join(columns)}) VALUES ({', '.join(':' + name for name in columns)})"), columns)

        for chart_id, (source, set_id) in sources.items():
            db.session.execute(
                text("INSERT INTO chart_sources (id, chart_id, source, checksum, source_set_id, created_at) VALUES (:id, :id, :source, :checksum, :set_id, :created_at)"),
                {"id": chart_id, "source": source, "checksum": f"sum{chart_id}", "set_id": set_id, "created_at": "2026-10-09 12:00:00"},
            )

        db.session.commit()
        flask_migrate.upgrade()
        rows = db.session.execute(text("SELECT id, downloadable FROM charts ORDER BY id")).all()
        assert [(row[0], bool(row[1])) for row in rows] == [(1, True), (2, False), (3, True), (4, False), (5, False)]
        assert schema_differences() == []

        flask_migrate.downgrade(revision="0010")
        assert "downloadable" not in {column["name"] for column in inspect(db.engine).get_columns("charts")}
        flask_migrate.upgrade()
        assert schema_differences() == []

    clean_up(app)
