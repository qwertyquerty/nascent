from .helpers import CHART, make_replay, rank_chart, submit


def test_global_stats(api, clock):
    empty = api.get("/api/v1/stats").get_json()
    assert empty["players"] == 0
    assert empty["plays"] == 0
    assert empty["top_plays"] == []

    alice = api.token("alice")
    submit(api, clock, alice, make_replay(CHART, seed=1))
    rank_chart(api)
    ranked = submit(api, clock, alice, make_replay(CHART, seed=2)).get_json()

    stats = api.get("/api/v1/stats").get_json()
    assert stats["players"] == 2
    assert stats["ranked_players"] == 1
    assert stats["plays"] == 2
    assert stats["ranked_plays"] == 1
    assert stats["charts"] == 1
    assert stats["ranked_charts"] == 1
    assert stats["total_playtime_ms"] > 0
    assert [play["id"] for play in stats["top_plays"]] == [ranked["play"]["id"]]
    assert stats["top_plays"][0]["user"]["username"] == "alice"
    assert stats["top_plays"][0]["chart"]["title"] == "Test Song"
