import pytest

from nascent_server.cli import nascent_cli
from nascent_server.config import Config
from nascent_server.extensions import db
from nascent_server.services import charts as chart_service
from nascent_server.services.ranking import profile_weights

from .helpers import CHART, HASH, make_chart, make_replay, rank_chart, submit

SECOND = make_chart(title="Second Song", note_count=220)


def login(api, username):
    return api.post("/api/v1/auth/login", {"login": username, "password": "correct horse battery"}).get_json()["tokens"]["access_token"]


def test_play_shows_its_place_on_the_chart_leaderboard(api, clock):
    rank_chart(api)
    alice = api.token("alice")
    bob = api.token("bob")
    best = submit(api, clock, alice, make_replay(CHART, seed=1, spread=5)).get_json()["play"]
    worse = submit(api, clock, alice, make_replay(CHART, seed=2, spread=60)).get_json()["play"]
    submit(api, clock, bob, make_replay(CHART, seed=3, spread=30))

    shown = api.get(f"/api/v1/plays/{best['id']}").get_json()
    assert shown["leaderboard"] == {"rank": 1, "total": 2, "rate": 1.0}
    assert api.get(f"/api/v1/plays/{worse['id']}").get_json()["leaderboard"] is None


def test_performance_and_rank_history(api, app, clock):
    rank_chart(api)
    rank_chart(api, SECOND)
    alice = api.token("alice")
    weights = profile_weights(Config.PROFILE_TOP_PLAYS, Config.PROFILE_DECAY)

    first = submit(api, clock, alice, make_replay(CHART, seed=1, spread=40)).get_json()["play"]
    clock.advance(days=1)
    alice = login(api, "alice")
    submit(api, clock, alice, make_replay(CHART, seed=2, spread=80))
    clock.advance(days=1)
    alice = login(api, "alice")
    second = submit(api, clock, alice, make_replay(SECOND, seed=3), chart=SECOND).get_json()["play"]

    history = api.get("/api/v1/users/alice/history?key_count=4").get_json()
    assert [day for day, _ in history["performance"]] == ["2026-10-09", "2026-10-11"]
    expected = sum(weight * value for weight, value in zip(weights, sorted([first["performance"], second["performance"]], reverse=True))) / sum(weights)
    assert history["performance"][-1][1] == pytest.approx(expected)
    assert history["rank"] == [["2026-10-09", 1], ["2026-10-10", 1], ["2026-10-11", 1]]
    assert api.get("/api/v1/users/alice/history").get_json()["error"]["code"] == "invalid_key_count"

    clock.advance(days=1)
    bob = api.token("bob")
    submit(api, clock, bob, make_replay(CHART, seed=4, spread=3))
    submit(api, clock, bob, make_replay(SECOND, seed=5, spread=3), chart=SECOND)
    result = app.test_cli_runner().invoke(nascent_cli, ["snapshot-ranks"])
    assert result.exit_code == 0, result.output
    assert "2 leaderboard entries" in result.output

    ranks = api.get("/api/v1/users/alice/history?key_count=4").get_json()["rank"]
    assert ranks[-1] == [clock.now.date().isoformat(), 2]


def test_activity_counts_days_and_favorite_charts(api, clock):
    alice = api.token("alice")
    submit(api, clock, alice, make_replay(CHART, seed=1))
    submit(api, clock, alice, make_replay(CHART, seed=2))
    clock.advance(days=2)
    alice = login(api, "alice")
    submit(api, clock, alice, make_replay(SECOND, seed=3), chart=SECOND)

    activity = api.get("/api/v1/users/alice/activity").get_json()
    assert activity["last_day"] == "2026-10-11"
    assert activity["first_day"] == "2025-10-12"
    assert activity["days"] == [["2026-10-09", 2], ["2026-10-11", 1]]
    favorites = activity["favorite_charts"]
    assert [entry["chart"]["title"] for entry in favorites] == ["Test Song", "Second Song"]
    assert [entry["plays"] for entry in favorites] == [2, 1]
    assert favorites[0]["best_accuracy"] > 0
    assert favorites[0]["last_played_at"].startswith("2026-10-09")


def test_osu_sources_list_mirror_downloads(api, app):
    rank_chart(api)

    with app.app_context():
        chart = chart_service.get_chart(HASH)
        chart_service.add_chart_source(chart, "osu", "abc123", source_id=42, source_set_id=7)
        db.session.commit()

    source = api.get(f"/api/v1/charts/{HASH}").get_json()["chart"]["sources"][0]
    assert source["url"] == "https://osu.ppy.sh/beatmapsets/7#mania/42"
    assert source["downloads"] == [
        {"name": "catboy.best", "url": "https://catboy.best/d/7"},
        {"name": "osu.direct", "url": "https://osu.direct/api/d/7"},
    ]
