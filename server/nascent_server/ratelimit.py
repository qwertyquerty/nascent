import redis
from flask import current_app

from .cache import store
from .errors import ApiError


def _hit(name: str, identity: str | int) -> tuple[bool, int]:
    limit, window = current_app.config["RATE_LIMITS"][name]

    try:
        count, retry_after = store().hit(f"{name}:{identity}", window)
    except redis.RedisError:
        current_app.logger.warning("rate limiter unavailable, allowing %s", name)
        return False, 0

    return count > limit, retry_after


def over_rate_limit(name: str, identity: str | int) -> bool:
    return _hit(name, identity)[0]


def check_rate_limit(name: str, identity: str | int) -> None:
    limited, retry_after = _hit(name, identity)

    if limited:
        raise ApiError(
            429,
            "rate_limited",
            "Too many requests, try again later",
            {"limit": name, "retry_after": retry_after},
            {"Retry-After": str(max(retry_after, 1))},
        )
