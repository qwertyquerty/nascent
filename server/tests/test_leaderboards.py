from .helpers import CHART, HASH, make_replay, rank_chart, submit


def test_chart_performance_leaderboard_combines_rates(api, clock):
    rank_chart(api)
    alice = api.token("alice")
    bob = api.token("bob")
    carol = api.token("carol")

    alice_normal = submit(api, clock, alice, make_replay(CHART, seed=1)).get_json()["play"]
    alice_fast = submit(api, clock, alice, make_replay(CHART, rate=1.5, seed=2)).get_json()["play"]
    bob_play = submit(api, clock, bob, make_replay(CHART, seed=3, spread=40)).get_json()["play"]
    submit(api, clock, carol, make_replay(CHART, rate=1.5, seed=4, spread=30))

    best_alice = max((alice_normal, alice_fast), key=lambda play: play["performance"])

    board = api.get(f"/api/v1/charts/{HASH}/leaderboard/performance").get_json()
    entries = board["entries"]
    assert board["total"] == 3
    assert [entry["play"]["user"]["username"] for entry in entries].count("alice") == 1
    assert [entry["play"]["performance"] for entry in entries] == sorted((entry["play"]["performance"] for entry in entries), reverse=True)
    assert [entry["rank"] for entry in entries] == [1, 2, 3]

    alice_entry = next(entry for entry in entries if entry["play"]["user"]["username"] == "alice")
    assert alice_entry["play"]["id"] == best_alice["id"]
    assert alice_entry["play"]["rate"] == best_alice["rate"]
    assert {entry["play"]["rate"] for entry in entries} >= {1.0, 1.5}
    assert bob_play["id"] in [entry["play"]["id"] for entry in entries]

    viewer = api.get(f"/api/v1/charts/{HASH}/leaderboard/performance", token=alice).get_json()["viewer"]
    assert viewer["rank"] == alice_entry["rank"]
    assert viewer["play"]["id"] == best_alice["id"]


def test_key_leaderboard_sorts_and_play_lists(api, clock):
    rank_chart(api)
    alice = api.token("alice")
    bob = api.token("bob")

    submit(api, clock, alice, make_replay(CHART, seed=1))
    submit(api, clock, bob, make_replay(CHART, seed=2, spread=40))
    submit(api, clock, bob, make_replay(CHART, rate=1.5, seed=3, spread=40))

    def sorted_board(sort):
        body = api.get(f"/api/v1/leaderboards/performance?key_count=4&sort={sort}").get_json()
        assert body["sort"] == sort
        return [entry["user"]["username"] for entry in body["entries"]]

    assert sorted_board("accuracy") == ["alice", "bob"]
    assert sorted_board("ranked_plays") == ["bob", "alice"]
    assert api.get("/api/v1/leaderboards/performance?key_count=4&sort=combo").get_json()["error"]["code"] == "invalid_query"

    recent = api.get("/api/v1/plays").get_json()
    assert recent["total"] == 3
    assert [play["id"] for play in recent["plays"]] == sorted((play["id"] for play in recent["plays"]), reverse=True)
    assert recent["plays"][0]["user"]["username"] == "bob"
    assert recent["plays"][0]["chart"]["hash"] == HASH

    top = api.get("/api/v1/plays?sort=performance").get_json()["plays"]
    assert [play["performance"] for play in top] == sorted((play["performance"] for play in top), reverse=True)
    assert all(play["ranked"] for play in top)
    assert api.get("/api/v1/plays?key_count=7").get_json()["total"] == 0
    assert api.get("/api/v1/plays?sort=oldest").get_json()["error"]["code"] == "invalid_query"
