def login(client, login_name, password, ip):
    return client.post("/api/v1/auth/login", json={"login": login_name, "password": password}, environ_base={"REMOTE_ADDR": ip})


def test_register_login_and_refresh(api, client):
    registered = api.register("alice", password="correct horse battery")
    assert registered["user"]["username"] == "alice"
    assert api.post("/api/v1/auth/register", {"username": "ALICE", "email": "other@example.com", "password": "correct horse battery"}).get_json()["error"]["code"] == "username_taken"

    assert login(client, "alice", "wrong password", "10.0.0.1").status_code == 401
    by_email = login(client, "alice@example.com", "correct horse battery", "10.0.0.1")
    assert by_email.status_code == 200

    refresh_token = by_email.get_json()["tokens"]["refresh_token"]
    rotated = api.post("/api/v1/auth/refresh", {"refresh_token": refresh_token})
    assert rotated.status_code == 200
    new_access = rotated.get_json()["tokens"]["access_token"]
    assert api.get("/api/v1/me", new_access).status_code == 200

    reused = api.post("/api/v1/auth/refresh", {"refresh_token": refresh_token})
    assert reused.status_code == 401
    assert api.get("/api/v1/me", new_access).status_code == 401


def test_failed_logins_only_lock_out_the_address_that_made_them(api, client):
    api.register("alice", password="correct horse battery")

    for _ in range(10):
        assert login(client, "alice", "wrong password", "10.0.0.66").status_code == 401

    assert login(client, "alice", "correct horse battery", "10.0.0.66").status_code == 429
    assert login(client, "alice", "correct horse battery", "10.0.0.7").status_code == 200


def test_sessions_can_be_listed_and_revoked(api, client):
    first = api.register("alice", password="correct horse battery")["tokens"]["access_token"]
    second = login(client, "alice", "correct horse battery", "10.0.0.2").get_json()["tokens"]["access_token"]

    sessions = api.get("/api/v1/auth/sessions", first).get_json()["sessions"]
    assert len(sessions) == 2
    other = next(session for session in sessions if not session["current"])

    assert api.client.delete(f"/api/v1/auth/sessions/{other['id']}", headers=api.headers(first)).status_code == 204
    assert api.get("/api/v1/me", second).status_code == 401
    assert api.get("/api/v1/me", first).status_code == 200

    assert api.post("/api/v1/auth/logout", token=first).status_code == 204
    assert api.get("/api/v1/me", first).status_code == 401
