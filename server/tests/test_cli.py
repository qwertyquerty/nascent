import json

from sqlalchemy import select

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


def stepmania_text(rows):
    measures = "\n,\n".join("\n".join(rows[index : index + 4]) for index in range(0, len(rows), 4))
    hard = "\n,\n".join("1000\n0100\n0010\n0001" for _ in range(len(rows) // 4))
    return (
        "#TITLE:Imported;\n#ARTIST:Someone;\n#MUSIC:song.ogg;\n#OFFSET:-0.5;\n#BPMS:0=120;\n"
        f"#NOTES:\n     dance-single:\n     Mapper:\n     Easy:\n     3:\n     0,0,0,0,0:\n{measures}\n;\n"
        f"#NOTES:\n     dance-single:\n     Mapper:\n     Hard:\n     9:\n     0,0,0,0,0:\n{hard}\n;\n"
        "#NOTES:\n     lights-cabinet:\n     :\n     Easy:\n     1:\n     0,0,0,0,0:\n00000000\n;\n"
    )


def test_import_stepmania_charts(api, app, tmp_path):
    from nascent_server.extensions import db
    from nascent_server.models import Chart, ChartSource, ChartStatus

    pack = tmp_path / "packs" / "42 Some Pack"
    song = pack / "Song"
    song.mkdir(parents=True)
    (pack / "pack.json").write_text(json.dumps({"id": 42, "site": "etternaonline"}))
    rows = ["1000", "0000", "0100", "0000"] * 16
    (song / "song.sm").write_text(stepmania_text(rows))
    (song / "song.ssc").write_text(stepmania_text(rows))
    other = pack / "Other"
    other.mkdir()
    (other / "other.dwi").write_text("#TITLE:Dwi;\n#ARTIST:Someone;\n#FILE:song.mp3;\n#BPM:150;\n#GAP:100;\n#SINGLE:MANIAC:8:" + "2468" * 32 + ";\n")
    tiny = pack / "Tiny"
    tiny.mkdir()
    (tiny / "tiny.sm").write_text("#TITLE:Tiny;\n#ARTIST:Someone;\n#OFFSET:-0.5;\n#BPMS:0=120;\n#NOTES:dance-single::Easy:1::1000\n0100\n0010\n0001\n;\n")

    result = run(app, "import-charts", str(tmp_path / "packs"), "--rank", "--quiet")
    assert result.exit_code == 0, result.output
    assert "4 new charts, 0 already on the server, 1 skipped, from 3 files" in result.output
    assert "skipped 1: steps type 'lights-cabinet' is not supported" in result.output

    with app.app_context():
        charts = db.session.scalars(select(Chart).order_by(Chart.title, Chart.difficulty_name)).all()
        assert [(chart.title, chart.difficulty_name) for chart in charts] == [("Dwi", "Hard"), ("Imported", "Easy"), ("Imported", "Hard"), ("Tiny", "Easy")]
        assert [chart.status for chart in charts] == [ChartStatus.RANKED, ChartStatus.RANKED, ChartStatus.RANKED, ChartStatus.UNRANKED]
        sources = db.session.scalars(select(ChartSource)).all()
        assert len(sources) == 4
        assert all(source.source == "etterna" and source.checksum.startswith("X") and len(source.checksum) == 41 for source in sources)
        assert sorted(source.source_set_id for source in sources) == [42, 42, 42, 42]
        first_hash = charts[0].hash

    detail = api.get(f"/api/v1/charts/{first_hash}").get_json()["chart"]
    assert detail["sources"][0]["url"] == "https://etternaonline.com/packs/42"
    assert detail["sources"][0]["downloads"] == []

    api.post("/api/v1/charts", make_chart(title="Player Upload", note_count=150), api.token("alice"))
    assert api.get("/api/v1/charts?source=etterna").get_json()["total"] == 4
    uploaded = api.get("/api/v1/charts?source=none").get_json()
    assert [chart["title"] for chart in uploaded["charts"]] == ["Player Upload"]
    assert uploaded["charts"][0]["sources"] == []
    assert api.get("/api/v1/charts?downloadable=1").get_json()["total"] == 3

    again = run(app, "import-charts", str(tmp_path / "packs"), "--rank", "--quiet")
    assert "0 new charts, 4 already on the server, 1 skipped, from 3 files" in again.output


def test_unrank_short_charts(api, app):
    from nascent_server.extensions import db
    from nascent_server.models import Chart, ChartStatus

    token = api.token("alice")
    short = api.post("/api/v1/charts", make_chart(title="Short", note_count=10), token).get_json()["chart"]["hash"]
    long = api.post("/api/v1/charts", make_chart(title="Long", note_count=200), token).get_json()["chart"]["hash"]

    with app.app_context():
        for chart in db.session.scalars(select(Chart)).all():
            chart.status = ChartStatus.RANKED

        db.session.commit()

    preview = run(app, "unrank-short-charts", "--dry-run")
    assert preview.exit_code == 0, preview.output
    assert "would unrank" in preview.output and "Short" in preview.output and "Long" not in preview.output
    assert "1 ranked charts have fewer than 15 notes" in preview.output

    result = run(app, "unrank-short-charts")
    assert result.exit_code == 0, result.output
    assert "1 charts unranked" in result.output

    with app.app_context():
        statuses = {chart.hash: chart.status for chart in db.session.scalars(select(Chart)).all()}
        assert statuses[short] == ChartStatus.UNRANKED
        assert statuses[long] == ChartStatus.RANKED
