from flask import Blueprint, g
from sqlalchemy import or_, select
from sqlalchemy.exc import IntegrityError

from .. import timeutil
from ..errors import ApiError
from ..extensions import db
from ..models import AuthSession, User
from ..ratelimit import check_rate_limit
from ..schemas import LoginIn, RefreshIn, RegisterIn, parse_body
from ..security import auth_required, client_ip, current_user, hash_password, issue_session, revoke_user_sessions, rotate_session, verify_password
from ..serializers import private_user

blueprint = Blueprint("auth", __name__, url_prefix="/auth")


@blueprint.post("/register")
def register():
    check_rate_limit("register", client_ip())
    body = parse_body(RegisterIn)
    username_key = body.username.lower()
    email_key = str(body.email).lower()

    taken = db.session.execute(select(User.username_key, User.email_key).where(or_(User.username_key == username_key, User.email_key == email_key))).all()

    if any(row.username_key == username_key for row in taken):
        raise ApiError(409, "username_taken", "That username is already taken")

    if taken:
        raise ApiError(409, "email_taken", "That email is already registered")

    user = User(username=body.username, username_key=username_key, email=str(body.email), email_key=email_key, password_hash=hash_password(body.password))
    db.session.add(user)

    try:
        db.session.flush()
    except IntegrityError:
        db.session.rollback()
        raise ApiError(409, "account_exists", "That username or email is already registered") from None

    tokens = issue_session(user)
    db.session.commit()
    return {"user": private_user(user), "tokens": tokens.to_dict()}, 201


@blueprint.post("/login")
def login():
    body = parse_body(LoginIn)
    login_key = body.login.lower()
    ip = client_ip()
    check_rate_limit("login_ip", ip)
    check_rate_limit("login_account", f"{login_key}:{ip}")

    user = db.session.scalar(select(User).where(or_(User.username_key == login_key, User.email_key == login_key)))

    if not verify_password(user, body.password):
        raise ApiError(401, "invalid_credentials", "The username, email or password is incorrect")

    tokens = issue_session(user)
    db.session.commit()
    return {"user": private_user(user), "tokens": tokens.to_dict()}


@blueprint.post("/refresh")
def refresh():
    check_rate_limit("refresh", client_ip())
    body = parse_body(RefreshIn)
    tokens = rotate_session(body.refresh_token)
    db.session.commit()
    return {"tokens": tokens.to_dict()}


@blueprint.post("/logout")
@auth_required
def logout():
    g.auth_session.revoked_at = timeutil.utcnow()
    db.session.commit()
    return "", 204


@blueprint.post("/logout-all")
@auth_required
def logout_all():
    revoke_user_sessions(current_user().id)
    db.session.commit()
    return "", 204


@blueprint.get("/sessions")
@auth_required
def sessions():
    user = current_user()
    now = timeutil.utcnow()
    rows = db.session.scalars(
        select(AuthSession)
        .where(AuthSession.user_id == user.id, AuthSession.revoked_at.is_(None), AuthSession.refresh_expires_at > now)
        .order_by(AuthSession.last_used_at.desc())
    )
    return {
        "sessions": [
            {
                "id": row.id,
                "client": row.client,
                "ip": row.ip,
                "created_at": timeutil.isoformat(row.created_at),
                "last_used_at": timeutil.isoformat(row.last_used_at),
                "current": row.id == g.auth_session.id,
            }
            for row in rows
        ]
    }


@blueprint.delete("/sessions/<int:session_id>")
@auth_required
def revoke_session(session_id: int):
    session = db.session.get(AuthSession, session_id)

    if session is None or session.user_id != current_user().id:
        raise ApiError(404, "session_not_found", "No such session")

    session.revoked_at = session.revoked_at or timeutil.utcnow()
    db.session.commit()
    return "", 204
