from sqlalchemy import select

from ..errors import ApiError
from ..extensions import db
from ..models import COUNTED_PLAY_STATUSES, Play, Role, User
from ..security import authenticate


def get_user_or_404(reference: str) -> User:
    if reference.isdigit():
        user = db.session.get(User, int(reference))
    else:
        user = db.session.scalar(select(User).where(User.username_key == reference.lower()))

    if user is None:
        raise ApiError(404, "user_not_found", "No such user")

    return user


def can_see_private(user: User) -> bool:
    viewer = authenticate()
    return viewer is not None and (viewer.id == user.id or viewer.has_role(Role.MODERATOR))


def get_visible_play_or_404(play_id: int) -> Play:
    play = db.session.get(Play, play_id)

    if play is None or (play.status not in COUNTED_PLAY_STATUSES and not can_see_private(play.user)):
        raise ApiError(404, "play_not_found", "No such play")

    return play
