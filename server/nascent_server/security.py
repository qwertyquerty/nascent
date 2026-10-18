import hashlib
import secrets
from dataclasses import dataclass
from functools import wraps

from argon2 import PasswordHasher
from argon2.exceptions import InvalidHashError, VerificationError
from flask import current_app, g, request
from sqlalchemy import select, update

from . import timeutil
from .errors import ApiError
from .extensions import db
from .models import AuthSession, Role, User

ACCESS_PREFIX = "nsa_"
REFRESH_PREFIX = "nsr_"
TICKET_PREFIX = "nst_"

password_hasher = PasswordHasher()
_dummy_hash = password_hasher.hash("nascent-dummy-password")


def hash_password(password: str) -> str:
    return password_hasher.hash(password)


def verify_password(user: User | None, password: str) -> bool:
    if user is None:
        try:
            password_hasher.verify(_dummy_hash, password)
        except VerificationError:
            pass
        return False

    try:
        password_hasher.verify(user.password_hash, password)
    except (VerificationError, InvalidHashError):
        return False

    if password_hasher.check_needs_rehash(user.password_hash):
        user.password_hash = password_hasher.hash(password)

    return True


def hash_token(token: str) -> str:
    return hashlib.sha256(token.encode()).hexdigest()


def new_token(prefix: str) -> str:
    return prefix + secrets.token_urlsafe(32)


@dataclass
class IssuedTokens:
    session: AuthSession
    access_token: str
    refresh_token: str

    def to_dict(self) -> dict:
        return {
            "token_type": "Bearer",
            "access_token": self.access_token,
            "access_expires_at": timeutil.isoformat(self.session.access_expires_at),
            "access_expires_in": max(0, int((self.session.access_expires_at - timeutil.utcnow()).total_seconds())),
            "refresh_token": self.refresh_token,
            "refresh_expires_at": timeutil.isoformat(self.session.refresh_expires_at),
        }


def client_ip() -> str:
    return (request.remote_addr or "")[:45]


def client_name() -> str:
    return (request.headers.get("User-Agent") or "").replace("\x00", "")[:200]


def issue_session(user: User) -> IssuedTokens:
    now = timeutil.utcnow()
    access_token = new_token(ACCESS_PREFIX)
    refresh_token = new_token(REFRESH_PREFIX)

    session = AuthSession(
        user_id=user.id,
        access_hash=hash_token(access_token),
        access_expires_at=now + current_app.config["ACCESS_TOKEN_TTL"],
        refresh_hash=hash_token(refresh_token),
        refresh_expires_at=now + current_app.config["REFRESH_TOKEN_TTL"],
        created_at=now,
        last_used_at=now,
        client=client_name(),
        ip=client_ip(),
    )
    db.session.add(session)
    db.session.flush()
    return IssuedTokens(session, access_token, refresh_token)


def rotate_session(refresh_token: str) -> IssuedTokens:
    token_hash = hash_token(refresh_token)
    now = timeutil.utcnow()
    session = db.session.scalar(select(AuthSession).where(AuthSession.refresh_hash == token_hash).with_for_update())

    if session is None:
        reused = db.session.scalar(select(AuthSession).where(AuthSession.previous_refresh_hash == token_hash))

        if reused is not None and reused.revoked_at is None:
            reused.revoked_at = now
            db.session.commit()

        raise ApiError(401, "invalid_refresh_token", "The refresh token is invalid or has expired")

    if session.revoked_at is not None or session.refresh_expires_at <= now:
        raise ApiError(401, "invalid_refresh_token", "The refresh token is invalid or has expired")

    access_token = new_token(ACCESS_PREFIX)
    new_refresh_token = new_token(REFRESH_PREFIX)
    session.previous_refresh_hash = session.refresh_hash
    session.refresh_hash = hash_token(new_refresh_token)
    session.access_hash = hash_token(access_token)
    session.access_expires_at = now + current_app.config["ACCESS_TOKEN_TTL"]
    session.refresh_expires_at = now + current_app.config["REFRESH_TOKEN_TTL"]
    session.last_used_at = now
    session.ip = client_ip()
    return IssuedTokens(session, access_token, new_refresh_token)


def revoke_user_sessions(user_id: int, keep_session_id: int | None = None) -> None:
    now = timeutil.utcnow()
    query = update(AuthSession).where(AuthSession.user_id == user_id, AuthSession.revoked_at.is_(None))

    if keep_session_id is not None:
        query = query.where(AuthSession.id != keep_session_id)

    db.session.execute(query.values(revoked_at=now))


def _bearer_token() -> str | None:
    header = request.headers.get("Authorization", "")

    if not header.startswith("Bearer "):
        return None

    token = header[7:].strip()
    return token or None


def _load_session(token: str) -> AuthSession | None:
    now = timeutil.utcnow()
    session = db.session.scalar(select(AuthSession).where(AuthSession.access_hash == hash_token(token)))

    if session is None or session.revoked_at is not None or session.access_expires_at <= now:
        return None

    if now - session.last_used_at >= current_app.config["SESSION_TOUCH_INTERVAL"]:
        session.last_used_at = now
        db.session.commit()

    return session


def authenticate() -> User | None:
    if "user" in g:
        return g.user

    token = _bearer_token()
    session = _load_session(token) if token else None
    g.auth_session = session
    g.user = session.user if session else None

    if token and session is None:
        raise ApiError(401, "invalid_token", "The access token is invalid or has expired", headers={"WWW-Authenticate": "Bearer"})

    return g.user


def current_user() -> User:
    user = authenticate()

    if user is None:
        raise ApiError(401, "authentication_required", "Authentication is required", headers={"WWW-Authenticate": "Bearer"})

    return user


def auth_required(view):
    @wraps(view)
    def wrapper(*args, **kwargs):
        current_user()
        return view(*args, **kwargs)

    return wrapper


def role_required(role: Role):
    def decorator(view):
        @wraps(view)
        def wrapper(*args, **kwargs):
            if not current_user().has_role(role):
                raise ApiError(403, "forbidden", "You do not have permission to do this")

            return view(*args, **kwargs)

        return wrapper

    return decorator


def require_not_banned(user: User) -> None:
    if user.is_banned:
        raise ApiError(403, "account_restricted", "This account is restricted")
