from .helpers import CHART, HASH, make_replay, rank_chart, submit


def moderator(api, app, username="mod_user"):
    from nascent_server.extensions import db
    from nascent_server.models import Role, User

    token = api.token(username)

    with app.app_context():
        db.session.query(User).filter_by(username_key=username).one().role = Role.MODERATOR
        db.session.commit()

    return token


def test_players_report_plays_players_and_charts(api, app, clock):
    rank_chart(api)
    alice = api.token("alice")
    bob = api.token("bob")
    play = submit(api, clock, alice, make_replay(CHART, seed=1)).get_json()["play"]

    made = api.post("/api/v1/reports", {"kind": "play", "target": str(play["id"]), "reason": "cheating", "details": "too perfect"}, bob)
    assert made.status_code == 201, made.get_json()
    assert made.get_json()["report"]["status"] == "open"
    assert made.get_json()["duplicate"] is False

    again = api.post("/api/v1/reports", {"kind": "play", "target": str(play["id"]), "reason": "other"}, bob)
    assert again.status_code == 200
    assert again.get_json()["duplicate"] is True
    assert again.get_json()["report"]["id"] == made.get_json()["report"]["id"]

    assert api.post("/api/v1/reports", {"kind": "player", "target": "alice", "reason": "offensive_name"}, bob).status_code == 201
    assert api.post("/api/v1/reports", {"kind": "chart", "target": HASH, "reason": "broken_timing"}, bob).status_code == 201

    assert api.post("/api/v1/reports", {"kind": "play", "target": str(play["id"]), "reason": "cheating"}, alice).get_json()["error"]["code"] == "own_report"
    assert api.post("/api/v1/reports", {"kind": "player", "target": "bob", "reason": "cheating"}, bob).status_code == 400
    assert api.post("/api/v1/reports", {"kind": "chart", "target": HASH, "reason": "cheating"}, bob).get_json()["error"]["code"] == "invalid_reason"
    assert api.post("/api/v1/reports", {"kind": "play", "target": "999999", "reason": "cheating"}, bob).status_code == 404
    assert api.post("/api/v1/reports", {"kind": "player", "target": "alice", "reason": "cheating"}).status_code == 401


def test_moderators_work_through_reports(api, app, clock):
    rank_chart(api)
    alice = api.token("alice")
    bob = api.token("bob")
    carol = api.token("carol")
    mod = moderator(api, app)
    play = submit(api, clock, alice, make_replay(CHART, seed=1)).get_json()["play"]

    api.post("/api/v1/reports", {"kind": "play", "target": str(play["id"]), "reason": "cheating"}, bob)
    api.post("/api/v1/reports", {"kind": "play", "target": str(play["id"]), "reason": "other", "details": "same play"}, carol)
    api.post("/api/v1/reports", {"kind": "chart", "target": HASH, "reason": "wrong_audio"}, bob)

    assert api.get("/api/v1/moderation/reports", bob).status_code == 403
    queue = api.get("/api/v1/moderation/reports", mod).get_json()
    assert queue["open"] == 3
    assert [report["kind"] for report in queue["reports"]] == ["play", "play", "chart"]
    assert queue["reports"][0]["play"]["user"]["username"] == "alice"
    assert queue["reports"][0]["user"]["username"] == "alice"
    assert queue["reports"][0]["reporter"]["username"] == "bob"
    assert queue["reports"][2]["chart"]["hash"] == HASH

    resolved = api.post(
        f"/api/v1/moderation/reports/{queue['reports'][0]['id']}/resolve", {"decision": "resolved", "note": "rejected the play"}, mod
    ).get_json()
    assert resolved["closed"] == 2
    assert resolved["report"]["status"] == "resolved"
    assert resolved["report"]["resolved_by"]["username"] == "mod_user"

    assert api.post(f"/api/v1/moderation/reports/{queue['reports'][1]['id']}/resolve", {"decision": "dismissed"}, mod).status_code == 409
    assert api.get("/api/v1/moderation/reports", mod).get_json()["open"] == 1
    closed = api.get("/api/v1/moderation/reports?status=resolved", mod).get_json()["reports"]
    assert {report["resolution_note"] for report in closed} == {"rejected the play"}
    assert len(closed) == 2

    assert api.post("/api/v1/moderation/reports/999999/resolve", {"decision": "dismissed"}, mod).status_code == 404
    assert api.get("/api/v1/moderation/reports?status=nope", mod).status_code == 400


def test_restricted_players_cannot_report(api, clock):
    admin = rank_chart(api)
    api.token("alice")
    bob = api.token("bob")
    assert api.put("/api/v1/moderation/users/bob/restriction", {"restricted": True}, admin).status_code == 200
    blocked = api.post("/api/v1/reports", {"kind": "player", "target": "alice", "reason": "cheating"}, bob)
    assert blocked.status_code == 403
    assert blocked.get_json()["error"]["code"] == "account_restricted"
