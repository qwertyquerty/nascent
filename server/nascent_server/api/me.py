from flask import Blueprint, current_app, g, jsonify

from ..errors import ApiError
from ..extensions import db
from ..ratelimit import check_rate_limit
from ..schemas import AccountDeleteIn, PasswordChangeIn, parse_body
from ..security import auth_required, current_user, hash_password, revoke_user_sessions, verify_password
from ..serializers import private_user
from ..services import accounts, email_verification
from ..tasks import enqueue

blueprint = Blueprint("me", __name__, url_prefix="/me")


@blueprint.get("")
@auth_required
def me():
    return {"user": private_user(current_user())}


@blueprint.post("/verify-email")
@auth_required
def send_verification_email():
    user = current_user()

    if user.email_verified_at is not None:
        return {"sent": False, "verified": True}

    if not email_verification.required():
        raise ApiError(409, "verification_off", "This server doesn't ask for email verification")

    check_rate_limit("verify_email_send", user.id)

    try:
        enqueue(email_verification.send_verification, user.id)
    except Exception:
        current_app.logger.exception("could not send a verification email to user %s", user.id)
        raise ApiError(503, "mail_unavailable", "The verification email couldn't be sent, try again later") from None

    return {"sent": True, "verified": False}, 202


@blueprint.get("/export")
@auth_required
def export():
    user = current_user()
    response = jsonify(accounts.export_account(user))
    response.headers["Content-Disposition"] = f'attachment; filename="nascent-{user.username}.json"'
    return response


@blueprint.delete("")
@auth_required
def delete_account():
    user = current_user()
    check_rate_limit("account_delete", user.id)
    body = parse_body(AccountDeleteIn)

    if not verify_password(user, body.password):
        raise ApiError(403, "invalid_credentials", "The password is incorrect")

    accounts.delete_account(user)
    return "", 204


@blueprint.put("/password")
@auth_required
def change_password():
    user = current_user()
    body = parse_body(PasswordChangeIn)

    if not verify_password(user, body.current_password):
        raise ApiError(403, "invalid_credentials", "The current password is incorrect")

    user.password_hash = hash_password(body.new_password)
    revoke_user_sessions(user.id, keep_session_id=g.auth_session.id)
    db.session.commit()
    return "", 204
