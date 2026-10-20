from .helpers import CHART, HASH, make_replay, rank_chart, submit


def ranked_play(api, clock, name="alice", seed=1):
    token = api.token(name)
    rank_chart(api)
    return submit(api, clock, token, make_replay(CHART, seed=seed)).get_json()


def test_user_search(api):
    for name in ("alice", "malice", "alicia", "bob"):
        api.token(name)

    names = [user["username"] for user in api.get("/api/v1/users?q=ali").get_json()["users"]]
    assert names[:2] == ["alice", "alicia"]
    assert set(names) == {"alice", "alicia", "malice"}
    assert api.get("/api/v1/users?q=").get_json()["users"] == []
    assert api.get("/api/v1/users?q=%25").get_json()["users"] == []

    admin = api.admin()
    assert api.put("/api/v1/moderation/users/malice/restriction", {"restricted": True}, admin).status_code == 200
    assert "malice" not in [user["username"] for user in api.get("/api/v1/users?q=ali").get_json()["users"]]


def test_profile_paging_and_ranked_players(api, clock):
    ranked_play(api, clock)
    alice = api.token("alice2")
    submit(api, clock, alice, make_replay(CHART, seed=2))

    mode = api.get("/api/v1/users/alice").get_json()["user"]["key_modes"][0]
    assert mode["ranked_players"] == 2
    assert mode["global_rank"] in (1, 2)

    best = api.get("/api/v1/users/alice/plays/best?key_count=4").get_json()
    assert best["total"] == 1

    recent = api.get("/api/v1/users/alice/plays/recent?limit=1&offset=0").get_json()
    assert recent["total"] == 1
    assert len(recent["plays"]) == 1
    assert api.get("/api/v1/users/alice/plays/recent?limit=1&offset=1").get_json()["plays"] == []


def test_play_timeline(api, clock):
    play = ranked_play(api, clock)["play"]
    timeline = api.get(f"/api/v1/plays/{play['id']}/timeline").get_json()

    assert timeline["play_id"] == play["id"]
    assert timeline["key_count"] == 4
    assert len(timeline["notes"]) == len([note for note in CHART["hit_objects"]])
    assert all({"lane", "time", "hit_score", "hit_error"} <= set(note) for note in timeline["notes"])
    assert timeline["inputs"]
    assert api.get("/api/v1/plays/999999/timeline").status_code == 404


def test_page_shell_meta(api, clock):
    play = ranked_play(api, clock)["play"]

    home = api.get("/").get_data(as_text=True)
    assert "<title>Nascent · A vertical scrolling rhythm game passion project</title>" in home
    assert 'property="og:description"' in home

    profile = api.get("/users/alice").get_data(as_text=True)
    assert "<title>alice · 4K #1 · Nascent</title>" in profile
    assert "4K #1 of 1" in profile

    chart = api.get(f"/charts/{HASH}").get_data(as_text=True)
    assert "Test Artist - Test Song [Hard]" in chart

    play_page = api.get(f"/plays/{play['id']}").get_data(as_text=True)
    assert "alice · " in play_page
    assert "on Test Artist - Test Song [Hard]" in play_page

    calculator = api.get("/difficulty").get_data(as_text=True)
    assert "<title>Difficulty calculator · Nascent</title>" in calculator
    assert "og:image" not in calculator
    assert '<meta property="og:description" content="Nascent Calc V2 analysis">' in calculator
    assert 'property="og:image"' in home

    missing = api.get("/users/nobody")
    assert missing.status_code == 404
    assert "<title>Page not found · Nascent</title>" in missing.get_data(as_text=True)

    unknown_api = api.get("/api/v1/nothing-here")
    assert unknown_api.status_code == 404
    assert unknown_api.get_json()["error"]["code"] == "not_found"
