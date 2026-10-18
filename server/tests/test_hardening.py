import gzip
import json

import nascent_core as nc
import redis

from nascent_server.api import plays as plays_api
from nascent_server.cache import MemoryStore
from nascent_server.services.plays import public_chart_path
from nascent_server.storage import storage

from .helpers import CHART, HASH, chart_hash, make_replay, submit, ticket_for
from .test_build_checks import OFFICIAL, samples

NUL = chr(0)


def post_raw(client, path, data, token=None, encoding=None):
    headers = {"Content-Type": "application/json"}

    if token:
        headers["Authorization"] = f"Bearer {token}"

    if encoding:
        headers["Content-Encoding"] = encoding

    return client.post(path, data=data, headers=headers)


def test_small_endpoints_refuse_large_bodies(api, client):
    api.register("alice", password="correct horse battery")
    bomb = gzip.compress(b"[" + b"[]," * 40_000 + b"0]")
    assert len(bomb) < 4096

    response = post_raw(client, "/api/v1/auth/login", bomb, encoding="gzip")
    assert response.status_code == 413
    assert response.get_json()["error"]["code"] == "body_too_large"

    padded = json.dumps({"login": "alice", "password": "x" * 70_000})
    assert post_raw(client, "/api/v1/auth/login", padded).status_code == 413
    assert api.post("/api/v1/auth/login", {"login": "alice", "password": "correct horse battery"}).status_code == 200


def test_login_is_rate_limited_before_its_body_is_read(app, client):
    limit = app.config["RATE_LIMITS"]["login_ip"][0]

    for _ in range(limit):
        assert post_raw(client, "/api/v1/auth/login", b"not json").status_code == 400

    assert post_raw(client, "/api/v1/auth/login", b"not json").status_code == 429


def test_deeply_nested_json_is_a_bad_request(client):
    response = post_raw(client, "/api/v1/auth/login", b"[" * 20_000 + b"]" * 20_000)
    assert response.status_code == 400
    assert response.get_json()["error"]["code"] == "invalid_json"


def test_large_bodies_are_capped_by_value_count(api, app, client, clock):
    token = api.token("player")
    body = {"chart_hash": HASH, "replay": make_replay(CHART), "chart": CHART, "ticket": ticket_for(api, token)}
    clock.advance(minutes=5)
    app.config["MAX_JSON_NODES"] = 100

    response = post_raw(client, "/api/v1/plays", json.dumps(body), token)
    assert response.status_code == 413
    assert response.get_json()["error"]["code"] == "body_too_complex"


def test_gzipped_submissions_are_still_accepted(api, client, clock):
    token = api.token("player")
    body = {"chart_hash": HASH, "replay": make_replay(CHART), "chart": CHART, "ticket": ticket_for(api, token)}
    clock.advance(minutes=5)

    response = post_raw(client, "/api/v1/plays", gzip.compress(json.dumps(body).encode()), token, encoding="gzip")
    assert response.status_code == 201, response.get_json()


def test_submissions_are_rate_limited_before_their_body_is_read(api, app, client):
    token = api.token("player")
    limit = app.config["RATE_LIMITS"]["submit_burst"][0]

    for _ in range(limit):
        assert post_raw(client, "/api/v1/plays", b"not json", token).status_code == 400

    assert post_raw(client, "/api/v1/plays", b"not json", token).status_code == 429


def test_timelines_are_computed_once_and_served_from_storage(api, client, clock, monkeypatch):
    token = api.token("player")
    play = submit(api, clock, token, make_replay(CHART)).get_json()["play"]

    first = api.get(f"/api/v1/plays/{play['id']}/timeline")
    assert first.status_code == 200
    assert first.headers["Cache-Control"] == f"public, max-age={plays_api.TIMELINE_MAX_AGE}"

    def fail(_play):
        raise AssertionError("the timeline was rebuilt")

    monkeypatch.setattr(plays_api, "build_timeline", fail)
    assert api.get(f"/api/v1/plays/{play['id']}/timeline").get_json() == first.get_json()

    compressed = client.get(f"/api/v1/plays/{play['id']}/timeline", headers={"Accept-Encoding": "gzip"})
    assert compressed.headers["Content-Encoding"] == "gzip"
    assert json.loads(gzip.decompress(compressed.data)) == first.get_json()


def test_stored_replays_drop_free_form_data(api, clock):
    token = api.token("player")
    replay = make_replay(CHART, seed=3)
    replay["chart"]["path"] = "C:/Users/Real Name/Downloads/song/chart.osu"
    replay["diagnostics"] = {**OFFICIAL, "nested": {"padding": [1] * 1000}, "note": "x" * 5000, "k" * 100: 1}
    replay["result"]["score_counts"]["PADDING"] = 7
    replay["junk"] = "x" * 5000
    replay["clock_samples"] = [[*sample, *range(50)] for sample in samples(1.0)]
    replay["pauses"] = [[1000.0, 2000.0, 3000.0, 4000.0]] * 1500

    response = submit(api, clock, token, replay)
    assert response.status_code == 201, response.get_json()
    play = response.get_json()["play"]
    assert play["status"] == "verified"

    stored = api.get(f"/api/v1/plays/{play['id']}/replay").get_json()
    assert stored["chart"]["path"] == "chart.osu"
    assert "junk" not in stored
    assert stored["diagnostics"]["version"] == OFFICIAL["version"]
    assert stored["diagnostics"]["note"] == "x" * 256
    assert "nested" not in stored["diagnostics"]
    assert "k" * 100 not in stored["diagnostics"]
    assert set(stored["result"]["score_counts"]) <= set(nc.HIT_SCORES)
    assert [len(sample) for sample in stored["clock_samples"]] == [2] * len(samples(1.0))
    assert len(stored["pauses"]) == 1000
    assert all(len(pause) == 3 for pause in stored["pauses"])


def test_public_chart_paths_never_leave_the_songs_folder():
    assert public_chart_path("Artist - Song/chart.osu") == "Artist - Song/chart.osu"
    assert public_chart_path("C:/Users/name/Downloads/chart.osu") == "chart.osu"
    assert public_chart_path("C:\\Users\\name\\chart.osu") == "chart.osu"
    assert public_chart_path("/home/name/songs/chart.json") == "chart.json"
    assert public_chart_path("//server/share/chart.osu") == "chart.osu"
    assert public_chart_path("~/songs/chart.osu") == "chart.osu"
    assert public_chart_path("../../../Users/name/chart.osu") == "chart.osu"
    assert public_chart_path("") == ""


def test_chart_uploads_bound_their_metadata(api):
    token = api.token("mapper")
    chart = json.loads(json.dumps(CHART))
    chart["info"]["title"] = "t" * 5000
    response = api.post("/api/v1/charts", chart, token)
    assert response.status_code == 201
    assert response.get_json()["chart"]["hash"] == chart_hash(CHART)
    assert len(response.get_json()["chart"]["title"]) == 255

    stored = json.loads(gzip.decompress(storage_bytes(api, response.get_json()["chart"]["hash"])))
    assert len(stored["info"]["title"]) == 1024

    crowded = json.loads(json.dumps(CHART))
    crowded["info"]["title"] = "Crowded"
    crowded["timing_points"] = [{"time": index, "beat_length": 500.0, "meter": 4} for index in range(200_001)]
    assert api.post("/api/v1/charts", crowded, token).status_code == 400


def storage_bytes(api, hash_value):
    from nascent_server.storage import sharded_key

    with api.app.app_context():
        return storage().get(sharded_key("charts", hash_value, ".json.gz"))


def test_malformed_lookups_are_not_server_errors(api, client):
    token = api.token("player")
    api.post("/api/v1/charts", CHART, token)
    huge = "9" * 25

    assert api.get(f"/api/v1/charts/{HASH}/leaderboard?rate=nan").status_code == 400
    assert api.get(f"/api/v1/charts/{HASH}/leaderboard?rate=inf").status_code == 400
    assert api.get("/api/v1/charts?min_difficulty=-inf").status_code == 400
    assert api.get(f"/api/v1/users/player/charts/{HASH}/plays?rate=nan").status_code == 400
    assert api.get(f"/api/v1/plays/{huge}").status_code == 404
    assert api.get(f"/api/v1/plays/{huge}/timeline").status_code == 404
    assert api.get(f"/api/v1/plays/{'9' * 5000}/replay").status_code == 404
    assert api.get(f"/api/v1/users/{huge}").status_code == 404
    assert api.get("/api/v1/users/%C2%B2").status_code == 404
    assert api.get("/api/v1/users/pl%00ayer").status_code == 404
    assert api.get("/api/v1/charts/%00").status_code == 404
    assert api.get(f"/api/v1/users/player/plays/recent?before={huge}").status_code == 200
    assert api.get("/api/v1/users?q=%00").status_code == 200
    assert api.get("/api/v1/charts?q=%00&source=%00").status_code == 200
    assert client.get(f"/plays/{huge}").status_code == 404
    assert client.get(f"/users/{huge}").status_code == 404
    assert api.post("/api/v1/auth/login", {"login": f"player{NUL}", "password": "whatever"}).status_code == 400


class BrokenRedis(MemoryStore):
    def hit(self, key, window):
        raise redis.ConnectionError("redis is down")


def test_rate_limits_hold_while_redis_is_down(api, app, client):
    app.extensions["nascent_store"] = BrokenRedis()
    limit = app.config["RATE_LIMITS"]["register"][0]

    for index in range(limit):
        api.register(f"player{index}")

    response = api.post("/api/v1/auth/register", {"username": "onemore", "email": "onemore@example.com", "password": "correct horse battery"})
    assert response.status_code == 429
