import re

from nascent_server.services import mail

from .helpers import CHART, make_replay, rank_chart, submit


def verify_link(app) -> str:
    with app.app_context():
        body = mail.outbox()[-1].get_content()

    return re.search(r"https://nascent\.test/verify#(\S+)", body).group(1)


def outbox_size(app) -> int:
    with app.app_context():
        return len(mail.outbox())


def test_verification_is_off_by_default(api, app, clock):
    rank_chart(api)
    token = api.token("alice")

    assert outbox_size(app) == 0
    assert api.get("/api/v1/info").get_json()["email_verification"] is False
    assert api.get("/api/v1/me", token).get_json()["user"]["email_verified"] is False
    assert submit(api, clock, token, make_replay(CHART, seed=1)).status_code == 201
    assert api.post("/api/v1/me/verify-email", token=token).get_json()["error"]["code"] == "verification_off"


def test_unverified_players_cannot_submit_until_they_open_the_link(api, app, clock):
    app.config["EMAIL_VERIFICATION"] = True
    rank_chart(api)
    token = api.token("alice")

    assert api.get("/api/v1/info").get_json()["email_verification"] is True
    blocked = submit(api, clock, token, make_replay(CHART, seed=1))
    assert blocked.status_code == 403
    assert blocked.get_json()["error"]["code"] == "email_unverified"

    link = verify_link(app)
    verified = api.post("/api/v1/auth/verify-email", {"token": link})
    assert verified.status_code == 200
    assert verified.get_json()["user"]["email_verified"] is True
    assert api.post("/api/v1/auth/verify-email", {"token": link}).status_code == 200
    assert submit(api, clock, token, make_replay(CHART, seed=2)).status_code == 201
    assert api.post("/api/v1/me/verify-email", token=token).get_json() == {"sent": False, "verified": True}


def test_links_expire_and_can_be_resent(api, app, clock):
    app.config["EMAIL_VERIFICATION"] = True
    api.register("alice")
    first = verify_link(app)
    clock.advance(days=7, seconds=1)

    assert api.post("/api/v1/auth/verify-email", {"token": first}).get_json()["error"]["code"] == "invalid_verification_token"

    login = api.post("/api/v1/auth/login", {"login": "alice", "password": "correct horse battery"})
    token = login.get_json()["tokens"]["access_token"]

    for _ in range(app.config["RATE_LIMITS"]["verify_email_send"][0]):
        assert api.post("/api/v1/me/verify-email", token=token).status_code == 202

    assert api.post("/api/v1/me/verify-email", token=token).status_code == 429
    assert api.post("/api/v1/auth/verify-email", {"token": verify_link(app)}).status_code == 200
    assert api.post("/api/v1/auth/verify-email", {"token": "nsv_made-up"}).status_code == 400
