import uuid
from datetime import UTC, datetime, timedelta

import pytest

from nascent_server import create_app, timeutil
from nascent_server.config import TestConfig
from nascent_server.extensions import db
from nascent_server.models import Role, User
from nascent_server.services import charts as chart_service


class Clock:
    def __init__(self):
        self.now = datetime(2026, 10, 9, 12, 0, tzinfo=UTC)

    def __call__(self) -> datetime:
        return self.now

    def advance(self, **kwargs) -> None:
        self.now += timedelta(**kwargs)


@pytest.fixture
def clock(monkeypatch):
    value = Clock()
    monkeypatch.setattr(timeutil, "utcnow", value)
    return value


@pytest.fixture
def app(tmp_path, clock):
    chart_service.clear_native_cache()
    app = create_app(TestConfig, {"STORAGE_PATH": str(tmp_path / "storage"), "REDIS_PREFIX": f"nascent-test:{uuid.uuid4().hex}:"})

    with app.app_context():
        db.create_all()

    yield app

    with app.app_context():
        db.drop_all()


@pytest.fixture
def client(app):
    return app.test_client()


class Api:
    def __init__(self, client, app):
        self.client = client
        self.app = app
        self.admin_token: str | None = None

    def register(self, username: str, password: str = "correct horse battery", email: str | None = None) -> dict:
        response = self.client.post("/api/v1/auth/register", json={"username": username, "email": email or f"{username}@example.com", "password": password})
        assert response.status_code == 201, response.get_json()
        return response.get_json()

    def token(self, username: str) -> str:
        return self.register(username)["tokens"]["access_token"]

    def headers(self, token: str) -> dict:
        return {"Authorization": f"Bearer {token}"}

    def get(self, path: str, token: str | None = None, **kwargs):
        return self.client.get(path, headers=self.headers(token) if token else {}, **kwargs)

    def post(self, path: str, body=None, token: str | None = None):
        return self.client.post(path, json=body, headers=self.headers(token) if token else {})

    def put(self, path: str, body=None, token: str | None = None):
        return self.client.put(path, json=body, headers=self.headers(token) if token else {})

    def admin(self) -> str:
        if self.admin_token is None:
            self.admin_token = self.token("admin_user")

            with self.app.app_context():
                db.session.query(User).filter_by(username_key="admin_user").one().role = Role.ADMIN
                db.session.commit()

        return self.admin_token


@pytest.fixture
def api(client, app):
    return Api(client, app)
