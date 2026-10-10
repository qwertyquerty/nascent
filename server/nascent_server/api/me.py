from flask import Blueprint, g

from ..errors import ApiError
from ..extensions import db
from ..schemas import PasswordChangeIn, parse_body
from ..security import auth_required, current_user, hash_password, revoke_user_sessions, verify_password
from ..serializers import private_user

blueprint = Blueprint("me", __name__, url_prefix="/me")


@blueprint.get("")
@auth_required
def me():
    return {"user": private_user(current_user())}


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
