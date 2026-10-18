from nascent_server.config import Config
from nascent_server.services import anticheat

from .helpers import CHART, make_replay, submit

CONFIG = {key: getattr(Config, key) for key in dir(Config) if key.startswith("ANTICHEAT_")}
OFFICIAL = {"version": "0.2.0.beta3", "platform": "Windows x86_64 MSVC 1951", "official_build": True, "build_commit": "a" * 40}


def codes(flags):
    return [(flag.code, flag.severity) for flag in flags]


def test_official_and_legacy_release_builds_pass():
    assert anticheat.build_flags(OFFICIAL, False, CONFIG) == []
    assert anticheat.build_flags({"version": "0.1.0.indev14", "platform": "Windows x86_64 MSVC 1951"}, False, CONFIG) == []
    assert anticheat.build_flags({"version": "0.2.0", "platform": "Linux x86_64 GCC 13.3"}, False, CONFIG) == []
    assert anticheat.build_flags({"version": "0.2.0.beta2", "platform": "macOS arm64 Clang 17.0"}, False, CONFIG) == []
    assert anticheat.build_flags(None, False, CONFIG) == []
    assert anticheat.build_flags({}, False, CONFIG) == []


def test_unofficial_builds_are_sent_for_review():
    modified = anticheat.build_flags({"version": "9.9.9-warp", "platform": "Windows x86_64 GCC 16.2"}, False, CONFIG)
    assert codes(modified) == [("unofficial_build", anticheat.REVIEW)]
    assert modified[0].data["reasons"] == ["version", "toolchain"]

    spoofed = anticheat.build_flags({**OFFICIAL, "platform": "Windows x86_64 GCC 16.2"}, False, CONFIG)
    assert spoofed[0].data["reasons"] == ["toolchain"]

    assert anticheat.build_flags({**OFFICIAL, "official_build": False}, False, CONFIG)[0].data["reasons"] == ["not_official"]
    assert anticheat.build_flags({**OFFICIAL, "build_commit": ""}, False, CONFIG)[0].data["reasons"] == ["missing_commit"]
    assert anticheat.build_flags({**OFFICIAL, "version": "0.0.0.ci42"}, False, CONFIG)[0].data["reasons"] == ["version"]


def test_staff_builds_and_disabled_setting_only_inform():
    dev = {"version": "0.0.0-dev", "platform": "Windows x86_64 MSVC 1951", "official_build": False, "build_commit": ""}
    assert codes(anticheat.build_flags(dev, True, CONFIG)) == [("unofficial_build", anticheat.INFO)]
    assert codes(anticheat.build_flags(dev, False, {**CONFIG, "ANTICHEAT_FLAG_UNOFFICIAL_BUILDS": False})) == [("unofficial_build", anticheat.INFO)]


def samples(ratio, count=30, pause_at=None):
    wall = 0.0
    song = -2000.0
    points = []

    for index in range(count):
        points.append([wall, song])
        step = 1000.0
        wall += step

        if index != pause_at:
            song += step * ratio

    return points


def test_clock_samples_catch_a_slowed_song():
    assert anticheat.clock_flags(samples(1.0), CONFIG) == []
    assert anticheat.clock_flags(samples(1.01, pause_at=10), CONFIG) == []
    assert codes(anticheat.clock_flags(samples(0.75), CONFIG)) == [("clock_rate_mismatch", anticheat.REVIEW)]
    assert codes(anticheat.clock_flags(samples(1.4), CONFIG)) == [("clock_rate_mismatch", anticheat.REVIEW)]


def test_missing_or_malformed_clock_samples_are_ignored():
    assert codes(anticheat.clock_flags(None, CONFIG)) == [("clock_samples_missing", anticheat.INFO)]
    assert anticheat.clock_flags(samples(0.5, count=4), CONFIG) == []
    assert anticheat.clock_flags([["a", "b"]] * 30, CONFIG) == []
    assert anticheat.clock_flags([[0, 0]] * 30, CONFIG) == []


def test_unofficial_build_submissions_wait_for_review(api, clock):
    admin = api.admin()
    token = api.token("modder")
    replay = make_replay(CHART, seed=5)
    replay["diagnostics"] = {"version": "9.9.9-warp", "platform": "Windows x86_64 GCC 16.2"}
    play = submit(api, clock, token, replay).get_json()["play"]
    assert play["status"] == "verified"
    assert play["flags"] == []

    reviewed = api.get(f"/api/v1/plays/{play['id']}", admin).get_json()["play"]
    assert reviewed["status"] == "pending_review"
    assert [flag["code"] for flag in reviewed["flags"]] == ["unofficial_build", "clock_samples_missing"]


def test_official_build_submissions_keep_their_new_fields(api, clock):
    token = api.token("player")
    replay = make_replay(CHART, seed=6)
    replay["diagnostics"] = OFFICIAL
    replay["input_times"] = [input[0] + 0.25 for input in replay["inputs"]]
    replay["clock_samples"] = samples(1.0)
    response = submit(api, clock, token, replay).get_json()
    assert response["play"]["status"] == "verified"

    stored = api.get(f"/api/v1/plays/{response['play']['id']}/replay").get_json()
    assert stored["input_times"][0] == replay["inputs"][0][0] + 0.25
    assert len(stored["clock_samples"]) == len(replay["clock_samples"])
