import time

import pytest

from nascent_server.cache import MemoryStore, RedisStore

from .helpers import CHART, HASH, make_replay, rank_chart, submit


class UnhealthyCache(MemoryStore):
    def ping(self):
        return False


def fake_redis_store():
    fakeredis = pytest.importorskip("fakeredis")
    store = RedisStore.__new__(RedisStore)
    store.client = fakeredis.FakeRedis()
    store.prefix = "nascent-test:"
    return store


@pytest.mark.parametrize("make_store", [MemoryStore, fake_redis_store], ids=["memory", "redis"])
def test_stores_keep_counts_until_they_expire_or_are_forgotten(make_store, monkeypatch):
    store = make_store()
    assert store.count_get("total") is None

    store.count_put("total", 7, 60)
    store.count_put("board", 3, 60, "chart:1")
    store.count_put("board", 4, 60, "chart:2")
    assert store.count_get("total") == 7
    assert store.count_get("board", "chart:1") == 3
    assert store.count_get("board") is None

    store.count_forget(["chart:1"])
    assert store.count_get("board", "chart:1") is None
    assert store.count_get("board", "chart:2") == 4
    assert store.count_get("total") == 7

    later_time, later_monotonic = time.time() + 61, time.monotonic() + 61
    monkeypatch.setattr(time, "time", lambda: later_time)
    monkeypatch.setattr(time, "monotonic", lambda: later_monotonic)
    assert store.count_get("board", "chart:2") is None


def test_totals_are_cached_and_chart_totals_clear_on_new_plays(api, app, clock):
    app.config["COUNT_CACHE_SECONDS"] = 60
    rank_chart(api)
    alice = api.token("alice")
    submit(api, clock, alice, make_replay(CHART, seed=1, spread=40))

    assert api.get("/api/v1/plays").get_json()["total"] == 1
    assert api.get("/api/v1/plays?key_count=7").get_json()["total"] == 0
    assert api.get(f"/api/v1/charts/{HASH}/leaderboard").get_json()["total"] == 1
    assert api.get("/api/v1/users/alice/plays/recent").get_json()["total"] == 1
    assert api.get(f"/api/v1/charts/{HASH}/leaderboard", token=alice).get_json()["viewer"]["rank"] == 1

    bob = api.token("bob")
    submit(api, clock, bob, make_replay(CHART, seed=2, spread=3))

    assert api.get("/api/v1/plays").get_json()["total"] == 1
    assert api.get("/api/v1/plays?sort=performance").get_json()["total"] == 2
    assert api.get(f"/api/v1/charts/{HASH}/leaderboard").get_json()["total"] == 2
    assert api.get(f"/api/v1/charts/{HASH}/leaderboard/performance").get_json()["total"] == 2
    assert api.get(f"/api/v1/charts/{HASH}/leaderboard", token=alice).get_json()["viewer"]["rank"] == 2

    submit(api, clock, alice, make_replay(CHART, seed=3, spread=60))
    assert api.get("/api/v1/users/alice/plays/recent").get_json()["total"] == 2

    app.config["COUNT_CACHE_SECONDS"] = 0
    assert api.get("/api/v1/plays").get_json()["total"] == 3


def test_health_is_cached_briefly(app, client):
    app.config["HEALTH_CACHE_SECONDS"] = 60
    healthy = client.get("/api/v1/health")
    assert healthy.status_code == 200
    assert healthy.get_json() == {"status": "ok", "database": True, "cache": True}

    app.extensions["nascent_store"] = UnhealthyCache()
    assert client.get("/api/v1/health").get_json() == healthy.get_json()

    app.config["HEALTH_CACHE_SECONDS"] = 0
    degraded = client.get("/api/v1/health")
    assert degraded.status_code == 503
    assert degraded.get_json() == {"status": "degraded", "database": True, "cache": False}
