from flask import Blueprint, g, jsonify

from ..errors import ApiError
from ..extensions import db
from ..ratelimit import check_rate_limit
from ..schemas import AccountDeleteIn, PasswordChangeIn, parse_body
from ..security import auth_required, current_user, hash_password, revoke_user_sessions, verify_password
from ..serializers import private_user
from ..services import accounts

blueprint = Blueprint("me", __name__, url_prefix="/me")


@blueprint.get("")
@auth_required
def me():
    return {"user": private_user(current_user())}


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
