import json

import nascent_core as nc

from nascent_server.config import Config
from nascent_server.services import anticheat

from .helpers import CHART, make_replay, rank_chart, submit

CONFIG = {key: getattr(Config, key) for key in dir(Config) if key.startswith("ANTICHEAT_")}


def codes(flags):
    return [flag.code for flag in flags]


def history(best, recent=(), count=20):
    return anticheat.PlayerHistory(count, best, list(recent))


def test_spikes_are_measured_against_the_established_best():
    assert codes(anticheat.performance_flags(68.0, history(55.1), CONFIG)) == []
    assert codes(anticheat.performance_flags(88.9, history(55.1, [68.0, 71.4]), CONFIG)) == ["performance_spike", "performance_streak"]
    assert codes(anticheat.performance_flags(106.2, history(55.1, [68.0, 71.4, 88.9, 103.6, 99.1]), CONFIG)) == ["performance_spike", "performance_streak"]


def test_a_streak_of_moderate_improvements_is_flagged():
    assert codes(anticheat.performance_flags(64.0, history(55.1, [68.0]), CONFIG)) == []
    assert codes(anticheat.performance_flags(64.0, history(55.1, [68.0, 71.4]), CONFIG)) == ["performance_streak"]
    assert codes(anticheat.performance_flags(58.0, history(55.1, [68.0, 71.4]), CONFIG)) == []


def test_players_without_an_established_history_use_the_new_account_rule():
    assert codes(anticheat.performance_flags(65.0, history(0.0, count=3), CONFIG)) == ["new_account_high_performance"]
    assert codes(anticheat.performance_flags(40.0, history(0.0, count=3), CONFIG)) == []


def past_plays():
    return [
        {"hold_ms": 50.0 + index % 15, "error_sd": 12.0 + (index % 5) * 0.5, "error_drift": 0.3 + (index % 5) * 0.03, "chord_spread_ms": 6.0 + index % 4}
        for index in range(30)
    ]


def test_fingerprints_flag_plays_unlike_the_player():
    past = past_plays()
    assert anticheat.fingerprint_flags({"hold_ms": 60.0, "error_sd": 14.5, "error_drift": 0.42, "chord_spread_ms": 7.5}, past, CONFIG) == []

    warped = anticheat.fingerprint_flags({"hold_ms": 23.0, "error_sd": 14.5, "error_drift": 0.42, "chord_spread_ms": 7.5}, past, CONFIG)
    assert codes(warped) == ["fingerprint_mismatch"]
    assert [entry["metric"] for entry in warped[0].data["metrics"]] == ["hold_ms"]

    robotic = anticheat.fingerprint_flags({"hold_ms": 60.0, "error_sd": 3.0, "error_drift": 0.0, "chord_spread_ms": 0.0}, past, CONFIG)
    assert sorted(entry["metric"] for entry in robotic[0].data["metrics"]) == ["chord_spread_ms", "error_drift", "error_sd"]


def test_fingerprints_need_enough_history():
    assert anticheat.fingerprint_flags({"hold_ms": 5.0}, past_plays()[:5], CONFIG) == []
    assert anticheat.fingerprint_flags({"hold_ms": 5.0}, [], CONFIG) == []


def test_fingerprints_are_measured_from_the_replay():
    replay = make_replay(CHART, seed=3)
    native = nc.Chart.from_json(json.dumps(CHART))
    parsed = nc.Replay.from_json(json.dumps(replay))
    measured = anticheat.fingerprint(nc.simulate(native, parsed), parsed.inputs)
    assert {"hold_ms", "error_sd", "error_drift"} <= set(measured)
    assert 35 <= measured["hold_ms"] <= 95


def test_review_state_is_private_but_the_play_still_counts(api, clock):
    admin = rank_chart(api)
    token = api.token("suspect")
    play = submit(api, clock, token, make_replay(CHART, seed=1, spread=0)).get_json()["play"]

    public = api.get(f"/api/v1/plays/{play['id']}").get_json()["play"]
    owner = api.get(f"/api/v1/plays/{play['id']}", token).get_json()["play"]
    staff = api.get(f"/api/v1/plays/{play['id']}", admin).get_json()["play"]

    assert public["status"] == owner["status"] == "verified"
    assert "flags" not in public
    assert owner["flags"] == []
    assert staff["status"] == "pending_review"
    assert staff["flags"]
    assert staff["fingerprint"] is not None
    assert public["ranked"] is True

    recent = api.get("/api/v1/users/suspect/plays/recent").get_json()["plays"]
    assert [entry["status"] for entry in recent] == ["verified"]
