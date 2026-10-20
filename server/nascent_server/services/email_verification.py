from flask import current_app
from sqlalchemy import select

from .. import timeutil
from ..errors import ApiError
from ..extensions import db
from ..models import EmailVerification, User
from ..security import hash_token, new_token
from ..tasks import task
from . import mail

TOKEN_PREFIX = "nsv_"

EMAIL = """Hi {username},

Open this link within {days} days to confirm that this is the email address for your Nascent account:

{link}

Until you do, your plays won't be submitted to the leaderboards. If you didn't make a Nascent account, ignore this email.
"""


def required() -> bool:
    return bool(current_app.config["EMAIL_VERIFICATION"]) and mail.enabled()


def require_verified(user: User) -> None:
    if required() and user.email_verified_at is None:
        raise ApiError(
            403,
            "email_unverified",
            "Verify your email to submit plays. Open the link we emailed you, or send a new one from your account page on the website",
        )


def issue(user: User) -> str:
    token = new_token(TOKEN_PREFIX)
    now = timeutil.utcnow()
    db.session.add(
        EmailVerification(
            user_id=user.id,
            token_hash=hash_token(token),
            email_key=user.email_key,
            created_at=now,
            expires_at=now + current_app.config["EMAIL_VERIFICATION_TTL"],
        )
    )
    return token


def redeem(token: str) -> User:
    verification = db.session.scalar(select(EmailVerification).where(EmailVerification.token_hash == hash_token(token)).with_for_update())
    now = timeutil.utcnow()

    if verification is not None and verification.used_at is not None and verification.user.email_verified_at is not None:
        return verification.user

    if verification is None or verification.used_at is not None or verification.expires_at <= now or verification.email_key != verification.user.email_key:
        raise ApiError(400, "invalid_verification_token", "This verification link has expired or was already used, send a new one from your account page")

    verification.used_at = now
    verification.user.email_verified_at = verification.user.email_verified_at or now
    return verification.user


@task
def send_verification(user_id: int) -> None:
    user = db.session.get(User, user_id)

    if user is None or user.email_verified_at is not None:
        return

    token = issue(user)
    db.session.commit()
    days = current_app.config["EMAIL_VERIFICATION_TTL"].days
    link = f"{current_app.config['SITE_URL']}/verify#{token}"
    mail.send(user.email, "Verify your Nascent email", EMAIL.format(username=user.username, days=days, link=link))
