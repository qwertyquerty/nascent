import json

import nascent_core as nc
import pytest

from nascent_server.config import Config
from nascent_server.services.ranking import profile_weights

from .helpers import CHART, HASH, make_chart, make_replay, rank_chart, submit, ticket_for

SEVEN = make_chart(key_count=7, title="Seven", note_count=260)


def board(api, key_count):
    return [entry["user"]["username"] for entry in api.get(f"/api/v1/leaderboards/performance?key_count={key_count}").get_json()["entries"]]


def test_ranked_play_flow(api, clock):
    alice = api.token("alice")
    missing = submit(api, clock, alice, make_replay(CHART), include_chart=False)
    assert missing.get_json()["error"]["code"] == "chart_required"

    first = submit(api, clock, alice, make_replay(CHART, seed=1)).get_json()
    assert first["chart_created"] is True
    assert first["play"]["ranked"] is False

    rank_chart(api)
    rank_chart(api, SEVEN)
    seven = submit(api, clock, alice, make_replay(SEVEN, seed=2), chart=SEVEN).get_json()
    bob = api.token("bob")
    submit(api, clock, bob, make_replay(CHART, seed=3, spread=40))

    assert "user" not in seven
    assert seven["key_mode"]["key_count"] == 7
    assert seven["key_mode"]["global_rank"] == 1

    weights = profile_weights(Config.PROFILE_TOP_PLAYS, Config.PROFILE_DECAY)
    profile = api.get("/api/v1/users/alice").get_json()["user"]
    modes = {mode["key_count"]: mode for mode in profile["key_modes"]}
    assert set(modes) == {4, 7}
    assert "performance" not in profile["stats"]
    assert "global_rank" not in profile["stats"]
    assert modes[4]["performance"] == pytest.approx(first["play"]["performance"] * weights[0] / sum(weights))
    assert modes[7]["performance"] == pytest.approx(seven["play"]["performance"] * weights[0] / sum(weights))
    assert modes[4]["global_rank"] == 1
    assert modes[7]["global_rank"] == 1

    assert board(api, 4) == ["alice", "bob"]
    assert board(api, 7) == ["alice"]
    assert api.get("/api/v1/leaderboards/performance").get_json()["error"]["code"] == "invalid_key_count"
    assert api.get("/api/v1/users/alice/plays/best").get_json()["error"]["code"] == "invalid_key_count"
    assert [entry["play"]["key_count"] for entry in api.get("/api/v1/users/alice/plays/best?key_count=7").get_json()["plays"]] == [7]

    leaderboard = api.get(f"/api/v1/charts/{HASH}/leaderboard").get_json()
    assert [entry["play"]["user"]["username"] for entry in leaderboard["entries"]] == ["alice", "bob"]


def test_bad_submissions_are_rejected(api, clock):
    token = api.token("cheater")

    tampered = make_replay(CHART, spread=40)
    tampered["result"]["accuracy"] = 1.0
    assert submit(api, clock, token, tampered).get_json()["error"]["code"] == "result_mismatch"

    assert submit(api, clock, token, make_replay(CHART), wait_minutes=0).get_json()["error"]["code"] == "play_too_fast"
    assert submit(api, clock, token, make_replay(CHART, end_extra=-5000)).get_json()["error"]["code"] == "incomplete_play"
    assert api.post("/api/v1/plays/tickets", {"chart_hash": HASH, "rate": 1.12}, token).get_json()["error"]["code"] == "invalid_rate"

    replay = make_replay(CHART, seed=5)
    assert submit(api, clock, token, replay).status_code == 201
    assert submit(api, clock, token, replay).get_json()["error"]["code"] == "duplicate_replay"

    stale = ticket_for(api, token)
    ticket_for(api, token)
    clock.advance(minutes=5)
    response = api.post("/api/v1/plays", {"chart_hash": HASH, "replay": make_replay(CHART, seed=6), "ticket": stale}, token).get_json()
    assert response["play"]["flags"][-1]["data"]["reason"] == "superseded"
    assert response["play"]["is_chart_best"] is False


def test_intro_skip_shortens_the_minimum_play_time(api, clock):
    chart = make_chart(title="Long Intro")

    for hit in chart["hit_objects"]:
        hit["time"] += 29000
        hit["end_time"] += 29000

    native = nc.Chart.from_json(json.dumps(chart))
    assert native.intro_skip_ms_at(1.0) == 30000 - nc.INTRO_SKIP_LEAD_MS
    assert nc.Chart.from_json(json.dumps(CHART)).intro_skip_ms_at(1.0) == 0
    playable_minutes = (native.end_ms_at(1.0) - native.intro_skip_ms_at(1.0)) / 60000
    token = api.token("skipper")

    early = submit(api, clock, token, make_replay(chart, seed=1), chart=chart, wait_minutes=playable_minutes * 0.8)
    assert early.status_code == 422
    assert early.get_json()["error"]["code"] == "play_too_fast"

    skipped = submit(api, clock, token, make_replay(chart, seed=2), chart=chart, wait_minutes=playable_minutes)
    assert skipped.status_code == 201, skipped.get_json()
