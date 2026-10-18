from flask import current_app
from sqlalchemy import select

from .. import timeutil
from ..errors import ApiError
from ..extensions import db
from ..models import PasswordReset, User
from ..security import hash_token, new_token
from ..tasks import task
from . import mail

TOKEN_PREFIX = "nsp_"

EMAIL = """Hi {username},

Someone asked to reset the password for your Nascent account. If that was you, open this link within {minutes} minutes to choose a new one:

{link}

If it wasn't you, ignore this email and your password will stay the same.
"""


def _password_check(user: User) -> str:
    return hash_token(user.password_hash)


def issue(user: User, ip: str) -> str:
    token = new_token(TOKEN_PREFIX)
    now = timeutil.utcnow()
    db.session.add(
        PasswordReset(
            user_id=user.id,
            token_hash=hash_token(token),
            password_check=_password_check(user),
            created_at=now,
            expires_at=now + current_app.config["PASSWORD_RESET_TTL"],
            ip=ip[:45],
        )
    )
    return token


def redeem(token: str) -> User:
    reset = db.session.scalar(select(PasswordReset).where(PasswordReset.token_hash == hash_token(token)).with_for_update())
    now = timeutil.utcnow()

    if reset is None or reset.used_at is not None or reset.expires_at <= now or reset.password_check != _password_check(reset.user):
        raise ApiError(400, "invalid_reset_token", "This reset link has expired or was already used, ask for a new one")

    reset.used_at = now
    return reset.user


@task
def send_password_reset(user_id: int, ip: str) -> None:
    user = db.session.get(User, user_id)

    if user is None:
        return

    token = issue(user, ip)
    db.session.commit()
    minutes = int(current_app.config["PASSWORD_RESET_TTL"].total_seconds() // 60)
    link = f"{current_app.config['SITE_URL']}/reset#{token}"
    mail.send(user.email, "Reset your Nascent password", EMAIL.format(username=user.username, minutes=minutes, link=link))
