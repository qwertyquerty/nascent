from .helpers import CHART, chart_hash, make_chart, make_replay, rank_chart, submit

UNRANKED = make_chart(title="Unranked Song", note_count=200)


def test_global_stats(api, clock):
    empty = api.get("/api/v1/stats").get_json()
    assert empty["players"] == 0
    assert empty["plays"] == 0
    assert empty["top_plays"] == []

    alice = api.token("alice")
    submit(api, clock, alice, make_replay(UNRANKED, seed=1), chart=UNRANKED)
    rank_chart(api)
    ranked = submit(api, clock, alice, make_replay(CHART, seed=2)).get_json()

    stats = api.get("/api/v1/stats").get_json()
    assert stats["players"] == 2
    assert stats["ranked_players"] == 1
    assert stats["plays"] == 2
    assert stats["ranked_plays"] == 1
    assert stats["charts"] == 2
    assert stats["ranked_charts"] == 1
    assert stats["total_playtime_ms"] > 0
    assert [play["id"] for play in stats["top_plays"]] == [ranked["play"]["id"]]
    assert stats["top_plays"][0]["user"]["username"] == "alice"
    assert stats["top_plays"][0]["chart"]["title"] == "Test Song"


def test_chart_list_filters_and_sorts(api):
    short = make_chart(title="Short Song", note_count=120)
    long = make_chart(title="Long Song", note_count=480)

    for chart in (short, CHART, long):
        rank_chart(api, chart)

    def listed(query):
        response = api.get(f"/api/v1/charts?{query}")
        assert response.status_code == 200, response.get_json()
        return [chart["hash"] for chart in response.get_json()["charts"]]

    by_length = listed("sort=length&order=asc")
    assert by_length == [chart_hash(short), chart_hash(CHART), chart_hash(long)]
    assert listed("sort=length&order=desc") == by_length[::-1]

    durations = {chart["hash"]: chart["duration_ms"] for chart in api.get("/api/v1/charts").get_json()["charts"]}
    middle = durations[chart_hash(CHART)] / 1000
    assert listed(f"min_duration={middle - 1}&max_duration={middle + 1}") == [chart_hash(CHART)]

    difficulties = {chart["hash"]: chart["difficulty"] for chart in api.get("/api/v1/charts").get_json()["charts"]}
    lowest = min(difficulties.values())
    assert set(listed(f"max_difficulty={lowest}")) == {hash_value for hash_value, value in difficulties.items() if value <= lowest}
    ordered = [difficulties[hash_value] for hash_value in listed("sort=difficulty&order=desc")]
    assert ordered == sorted(difficulties.values(), reverse=True)
