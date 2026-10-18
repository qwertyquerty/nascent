from sqlalchemy import select

from nascent_server.extensions import db
from nascent_server.models import AuthSession, Chart, Play, User
from nascent_server.storage import storage

from .helpers import CHART, HASH, make_replay, rank_chart, submit


def test_players_can_export_and_delete_their_account(api, app, clock):
    rank_chart(api)
    alice = api.token("alice")
    bob = api.token("bob")
    submit(api, clock, alice, make_replay(CHART, seed=1))
    submit(api, clock, bob, make_replay(CHART, seed=2))

    export = api.get("/api/v1/me/export", alice)
    assert export.headers["Content-Disposition"] == 'attachment; filename="nascent-alice.json"'
    data = export.get_json()
    assert data["user"]["email"] == "alice@example.com"
    assert len(data["plays"]) == 1 and data["sessions"][0]["ip"]

    with app.app_context():
        alice_id = db.session.scalar(select(User.id).where(User.username_key == "alice"))
        replay_key = db.session.scalar(select(Play.replay_key).where(Play.user_id == alice_id))
        assert storage().exists(replay_key)

    assert api.client.delete("/api/v1/me", json={"password": "wrong password"}, headers=api.headers(alice)).status_code == 403
    assert api.client.delete("/api/v1/me", json={"password": "correct horse battery"}, headers=api.headers(alice)).status_code == 204

    assert api.get("/api/v1/me", alice).status_code == 401
    assert api.get("/api/v1/users/alice").status_code == 404
    assert [entry["user"]["username"] for entry in api.get("/api/v1/leaderboards/performance?key_count=4").get_json()["entries"]] == ["bob"]
    assert [entry["play"]["user"]["username"] for entry in api.get(f"/api/v1/charts/{HASH}/leaderboard").get_json()["entries"]] == ["bob"]

    with app.app_context():
        assert db.session.query(User).filter_by(username_key="alice").count() == 0
        assert db.session.query(AuthSession).filter_by(user_id=alice_id).count() == 0
        assert db.session.query(Play).filter_by(user_id=alice_id).count() == 0
        assert db.session.query(Chart).filter_by(hash=HASH).one().play_count == 1
        assert not storage().exists(replay_key)

    assert api.register("alice")["user"]["username"] == "alice"
