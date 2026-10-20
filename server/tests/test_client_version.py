from nascent_server.versions import parse_release_version

from .helpers import CHART, make_replay, rank_chart, submit


def replay_from(version, seed):
    replay = make_replay(CHART, seed=seed)

    if version is not None:
        replay["diagnostics"] = {"version": version, "platform": "Windows x86_64 MSVC 1951", "official_build": True}

    return replay


def test_release_versions_order_like_the_game():
    order = ["0.1.0.indev14", "0.2.0.beta3", "0.2.0.beta10", "0.2.0.rc1", "0.2.0", "0.2.1", "1.0.0"]
    parsed = [parse_release_version(text) for text in order]
    assert parsed == sorted(parsed)
    assert parse_release_version("0.0.0-dev") is None
    assert parse_release_version("01.2.3") is None


def test_old_clients_are_told_to_update_before_submitting(api, app, clock):
    rank_chart(api)
    token = api.token("alice")
    assert api.get("/api/v1/info").get_json()["min_client_version"] is None
    assert submit(api, clock, token, replay_from("0.1.0.indev14", 1)).status_code == 201

    app.config["MIN_CLIENT_VERSION"] = "0.2.0.beta5"
    assert api.get("/api/v1/info").get_json()["min_client_version"] == "0.2.0.beta5"

    outdated = submit(api, clock, token, replay_from("0.2.0.beta4", 2))
    assert outdated.status_code == 426
    assert outdated.get_json()["error"]["code"] == "client_outdated"
    assert "You can keep playing" in outdated.get_json()["error"]["message"]
    assert submit(api, clock, token, replay_from(None, 3)).status_code == 426

    for seed, version in enumerate(("0.2.0.beta5", "0.3.0", "0.0.0-dev"), start=4):
        assert submit(api, clock, token, replay_from(version, seed)).status_code == 201, version
