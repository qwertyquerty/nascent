import hashlib
import json
from functools import cache

import nascent_core as nc
from flask import current_app

from ..errors import ApiError

RATE_STEP_KEY = round(nc.RATE_STEP * 100)
RATE_MIN_KEY = round(nc.RATE_MIN * 100)
RATE_MAX_KEY = round(nc.RATE_MAX * 100)
RATE_KEYS = tuple(range(RATE_MIN_KEY, RATE_MAX_KEY + 1, RATE_STEP_KEY))
DEFAULT_RATE_KEY = 100
RATE_TOLERANCE = 0.01


def rate_key_for(rate: float) -> int | None:
    scaled = rate * 100
    key = round(scaled)

    if abs(scaled - key) > RATE_TOLERANCE or key not in RATE_KEYS:
        return None

    return key


def require_rate_key(rate: float) -> int:
    rate_key = rate_key_for(rate)

    if rate_key is None:
        raise ApiError(400, "invalid_rate", f"Rates must be between {nc.RATE_MIN:g}x and {nc.RATE_MAX:g}x in steps of {nc.RATE_STEP:.2f}")

    return rate_key


def rate_for_key(rate_key: int) -> float:
    return rate_key / 100


@cache
def _calculator_version(judge_revision: int) -> str:
    payload = json.dumps(
        {
            "difficulty": nc.difficulty_fingerprint(),
            "chart_hash": nc.CHART_HASH_FORMAT,
            "performance_floor": nc.PERFORMANCE_ACCURACY_FLOOR,
            "judge": judge_revision,
        },
        sort_keys=True,
    )
    return hashlib.sha256(payload.encode()).hexdigest()[:12]


def calculator_version() -> str:
    return _calculator_version(current_app.config["JUDGE_REVISION"])


def performance(difficulty: float, accuracy: float) -> float:
    return nc.performance(difficulty, accuracy)
