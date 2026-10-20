import json

from nascent_server.cli import nascent_cli

from .helpers import CHART, make_chart, make_replay, rank_chart


def run(app, *args, input=None):
    return app.test_cli_runner().invoke(nascent_cli, list(args), input=input)


def test_set_password(api, app):
    token = api.token("alice")

    result = run(app, "set-password", "alice", "--password-stdin", input="a brand new password\n")
    assert result.exit_code == 0, result.output
    assert api.get("/api/v1/me", token).status_code == 401
    assert api.post("/api/v1/auth/login", {"login": "alice", "password": "a brand new password"}).status_code == 200

    assert run(app, "set-password", "alice", "--password-stdin", input="short\n").exit_code != 0
    assert run(app, "set-password", "nobody", "--password-stdin", input="a brand new password\n").exit_code != 0


def test_import_replays(api, app, tmp_path):
    api.token("alice")
    admin = rank_chart(api)
    api.post("/api/v1/charts", make_chart(note_count=200), admin)

    folder = tmp_path / "replays" / "0123456789abcdef"
    folder.mkdir(parents=True)
    (folder / "one.json").write_text(json.dumps(make_replay(CHART, seed=1)))
    (folder / "two.json").write_text(json.dumps(make_replay(CHART, seed=2)))
    (folder / "unknown.json").write_text(json.dumps(make_replay(make_chart(title="Not Uploaded", note_count=180), seed=3)))
    (folder / "settings.json").write_text("{}")

    result = run(app, "import-replays", str(tmp_path / "replays"), "--user", "alice")
    assert result.exit_code == 0, result.output
    assert "2 added, 0 already on the server, 1 skipped, 1 not replays" in result.output

    plays = api.get("/api/v1/users/alice/plays/recent").get_json()["plays"]
    assert len(plays) == 2
    assert all(play["ranked"] for play in plays)
    assert {play["submitted_at"] for play in plays} == {"2026-10-09T12:00:00Z"}

    again = run(app, "import-replays", str(tmp_path / "replays"), "--user", "alice")
    assert "0 added, 2 already on the server, 1 skipped, 1 not replays" in again.output


def test_calculator_status(api, app):
    rank_chart(api)

    result = run(app, "calculator-status", "--require-current")
    assert result.exit_code == 0, result.output
    assert "charts: 1 on the current calculator, 0 on an older one" in result.output

    with app.app_context():
        from sqlalchemy import update

        from nascent_server.extensions import db
        from nascent_server.models import Chart

        db.session.execute(update(Chart).values(calculator_version="older"))
        db.session.commit()

    stale = run(app, "calculator-status", "--require-current")
    assert stale.exit_code != 0
    assert "charts: 0 on the current calculator, 1 on an older one" in stale.output


def test_wait_for_jobs_inline(app):
    result = run(app, "wait-for-jobs")
    assert result.exit_code == 0, result.output
    assert "nothing to wait for" in result.output


def test_difficulty_report(api, app):
    rank_chart(api)

    before = run(app, "difficulty-report")
    assert before.exit_code == 0, before.output
    report = json.loads(before.output)
    assert report["boards"]["4"]["chart_difficulty"]["count"] == 1

    compared = run(app, "difficulty-report", "--compare-to", "-", input=before.output)
    assert compared.exit_code == 0, compared.output
    assert f"calculator {report['calculator']} -> {report['calculator']}" in compared.output
    assert "4K: 1 ranked charts" in compared.output


def test_calculator_version_follows_the_difficulty_code(monkeypatch, app):
    import nascent_core as nc

    from nascent_server.services import calculator

    with app.app_context():
        original = calculator.calculator_version()
        calculator._calculator_version.cache_clear()
        monkeypatch.setattr(nc, "difficulty_fingerprint", lambda: "a different calculator")
        changed = calculator.calculator_version()
        calculator._calculator_version.cache_clear()

    assert changed != original
