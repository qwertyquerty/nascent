import re

from nascent_server.services import mail

from .test_auth import login


def reset_link(app) -> str:
    with app.app_context():
        body = mail.outbox()[-1].get_content()

    return re.search(r"https://nascent\.test/reset#(\S+)", body).group(1)


def request_reset(client, login_name, ip="10.0.0.1"):
    return client.post("/api/v1/auth/password-reset", json={"login": login_name}, environ_base={"REMOTE_ADDR": ip})


def confirm(api, token, password="a brand new password"):
    return api.post("/api/v1/auth/password-reset/confirm", {"token": token, "new_password": password})


def test_reset_link_changes_the_password_and_signs_out_everywhere(api, app, client):
    old_access = api.register("alice", password="correct horse battery")["tokens"]["access_token"]

    assert request_reset(client, "ALICE@example.com").status_code == 202

    with app.app_context():
        message = mail.outbox()[-1]
        assert message["To"] == "alice@example.com"
        assert "Hi alice" in message.get_content()

    response = confirm(api, reset_link(app))
    assert response.status_code == 200
    assert response.get_json()["user"]["username"] == "alice"
    assert api.get("/api/v1/me", response.get_json()["tokens"]["access_token"]).status_code == 200

    assert api.get("/api/v1/me", old_access).status_code == 401
    assert login(client, "alice", "correct horse battery", "10.0.0.2").status_code == 401
    assert login(client, "alice", "a brand new password", "10.0.0.2").status_code == 200


def test_reset_links_work_once_and_expire(api, app, client, clock):
    api.register("alice")
    request_reset(client, "alice")
    token = reset_link(app)

    assert confirm(api, token).status_code == 200
    assert confirm(api, token, "another new password").get_json()["error"]["code"] == "invalid_reset_token"

    request_reset(client, "alice")
    clock.advance(hours=1, seconds=1)
    assert confirm(api, reset_link(app)).get_json()["error"]["code"] == "invalid_reset_token"


def test_changing_the_password_cancels_outstanding_links(api, app, client):
    access = api.register("alice", password="correct horse battery")["tokens"]["access_token"]
    request_reset(client, "alice")
    token = reset_link(app)

    assert api.put("/api/v1/me/password", {"current_password": "correct horse battery", "new_password": "changed it myself"}, access).status_code == 204
    assert confirm(api, token).get_json()["error"]["code"] == "invalid_reset_token"


def test_unknown_accounts_look_the_same_and_resets_are_rate_limited(api, app, client):
    api.register("alice")

    assert request_reset(client, "nobody@example.com").get_json() == request_reset(client, "alice").get_json()

    for index in range(5):
        assert request_reset(client, "alice", ip=f"10.0.1.{index}").status_code == 202

    with app.app_context():
        assert len(mail.outbox()) == app.config["RATE_LIMITS"]["password_reset_account"][0]

    for _ in range(10):
        request_reset(client, "nobody", ip="10.0.2.1")

    assert request_reset(client, "nobody", ip="10.0.2.1").status_code == 429
    assert confirm(api, "nsp_made-up").status_code == 400


def test_resets_are_refused_when_mail_is_not_set_up(api, app, client):
    app.config["MAIL_BACKEND"] = "none"
    api.register("alice")

    assert api.get("/api/v1/info").get_json()["password_reset"] is False
    assert request_reset(client, "alice").get_json()["error"]["code"] == "mail_unavailable"


def test_send_test_email_command(app):
    result = app.test_cli_runner().invoke(args=["nascent", "send-test-email", "admin@example.com"])
    assert result.exit_code == 0, result.output

    with app.app_context():
        assert mail.outbox()[-1]["To"] == "admin@example.com"

    app.config["MAIL_BACKEND"] = "none"
    assert "mail is not set up" in app.test_cli_runner().invoke(args=["nascent", "send-test-email", "admin@example.com"]).output
