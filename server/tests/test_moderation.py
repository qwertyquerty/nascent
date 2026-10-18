from .helpers import CHART, make_replay, rank_chart, submit


def board(api):
    return [entry["user"]["username"] for entry in api.get("/api/v1/leaderboards/performance?key_count=4").get_json()["entries"]]


def test_robotic_plays_wait_for_review(api, clock):
    admin = rank_chart(api)
    token = api.token("robot")

    play = submit(api, clock, token, make_replay(CHART, seed=1, spread=0)).get_json()["play"]
    assert play["status"] == "verified"
    assert play["ranked"] is True
    assert play["flags"] == []
    assert board(api) == ["robot"]

    queue = api.get("/api/v1/moderation/plays", admin).get_json()["plays"]
    assert [entry["id"] for entry in queue] == [play["id"]]
    assert queue[0]["status"] == "pending_review"
    assert "robotic_hit_timing" in [flag["code"] for flag in queue[0]["flags"]]
    assert api.get("/api/v1/moderation/plays", token).status_code == 403

    rejected = api.post(f"/api/v1/moderation/plays/{play['id']}/review", {"decision": "reject", "note": "bot"}, admin).get_json()["play"]
    assert rejected["status"] == "rejected"
    assert rejected["ranked"] is False
    assert board(api) == []
    assert api.get("/api/v1/moderation/plays", admin).get_json()["plays"] == []


def test_approved_reviews_stay_ranked(api, clock):
    admin = rank_chart(api)
    token = api.token("steady")

    play = submit(api, clock, token, make_replay(CHART, seed=1, spread=0)).get_json()["play"]
    approved = api.post(f"/api/v1/moderation/plays/{play['id']}/review", {"decision": "approve"}, admin).get_json()["play"]
    assert approved["status"] == "verified"
    assert approved["ranked"] is True
    assert board(api) == ["steady"]


def test_restricted_players_leave_the_leaderboards(api, clock):
    admin = rank_chart(api)
    alice = api.token("alice")
    submit(api, clock, alice, make_replay(CHART, seed=1))
    assert board(api) == ["alice"]

    assert api.put("/api/v1/moderation/users/alice/restriction", {"restricted": True}, admin).status_code == 200
    assert board(api) == []
    assert api.get(f"/api/v1/charts/{play_chart_hash(api)}/leaderboard").get_json()["entries"] == []
    blocked = api.post("/api/v1/plays/tickets", {"chart_hash": play_chart_hash(api), "rate": 1.0}, alice)
    assert blocked.status_code == 403
    assert blocked.get_json()["error"]["code"] == "account_restricted"


def play_chart_hash(api):
    return api.get("/api/v1/charts?status=ranked").get_json()["charts"][0]["hash"]
