import os
from datetime import timedelta


class Config:
    SQLALCHEMY_DATABASE_URI = os.environ.get("NASCENT_DATABASE_URL", "sqlite:///nascent-dev.db")
    SQLALCHEMY_ENGINE_OPTIONS = {"pool_pre_ping": True}
    SQLALCHEMY_TRACK_MODIFICATIONS = False

    REDIS_URL = os.environ.get("NASCENT_REDIS_URL")
    REDIS_PREFIX = "nascent:"

    TASKS_INLINE = os.environ.get("NASCENT_TASKS_INLINE", "1" if not os.environ.get("NASCENT_REDIS_URL") else "0") == "1"
    TASK_QUEUE = "nascent"
    TASK_RETRIES = (10, 60, 300)
    TASK_BATCH_SIZE = 200
    RECALCULATE_BATCH_SIZE = 50
    RECALCULATE_JOB_TIMEOUT = 3600

    STORAGE_PATH = os.environ.get("NASCENT_STORAGE_PATH", os.path.abspath("storage"))
    USE_X_ACCEL_REDIRECT = os.environ.get("NASCENT_USE_X_ACCEL_REDIRECT", "0") == "1"
    X_ACCEL_PREFIX = "/_storage/"

    TRUST_PROXY_HOPS = int(os.environ.get("NASCENT_TRUST_PROXY_HOPS", "0"))
    CORS_ORIGINS = [origin for origin in os.environ.get("NASCENT_CORS_ORIGINS", "").split(",") if origin]

    MAX_CONTENT_LENGTH = 16 * 1024 * 1024
    MAX_DECOMPRESSED_BODY = 64 * 1024 * 1024

    ACCESS_TOKEN_TTL = timedelta(hours=1)
    REFRESH_TOKEN_TTL = timedelta(days=90)
    SESSION_TOUCH_INTERVAL = timedelta(minutes=5)

    TICKET_GRACE = timedelta(hours=1)
    TICKET_MIN_ELAPSED_RATIO = 0.95

    PROFILE_TOP_PLAYS = 40
    PROFILE_DECAY = 0.9
    RANKED_KEY_COUNTS = tuple(range(4, 11))

    JUDGE_REVISION = 1

    RATE_LIMITS = {
        "register": (5, 3600),
        "login_ip": (30, 600),
        "login_account": (10, 600),
        "refresh": (60, 600),
        "ticket": (2000, 3600),
        "submit_burst": (6, 60),
        "submit": (120, 3600),
        "chart_upload": (30, 3600),
    }

    MAX_REPLAY_INPUTS_PER_NOTE = 6
    MAX_REPLAY_EXTRA_INPUTS = 2000
    MAX_INPUT_BACKSTEP_MS = 50
    MAX_LEAD_IN_MS = 60_000
    MAX_END_OVERRUN_MS = 120_000

    ANTICHEAT_MIN_SAMPLES = 100
    ANTICHEAT_MIN_HIT_STDEV_MS = 3.0
    ANTICHEAT_MIN_TAP_DURATION_STDEV_MS = 2.0
    ANTICHEAT_MAX_IDENTICAL_CHORD_RATIO = 0.95
    ANTICHEAT_MIN_CHORDS = 30
    ANTICHEAT_MIN_REPEAT_INTERVAL_MS = 20
    ANTICHEAT_MAX_FAST_REPEATS = 12
    ANTICHEAT_SPIKE_MIN_RANKED_PLAYS = 10
    ANTICHEAT_SPIKE_RATIO = 1.25
    ANTICHEAT_SPIKE_MARGIN = 5.0
    ANTICHEAT_NEW_ACCOUNT_PERFORMANCE = 60.0


class TestConfig(Config):
    TESTING = True
    SQLALCHEMY_DATABASE_URI = os.environ.get("NASCENT_TEST_DATABASE_URL", "sqlite://")
    SQLALCHEMY_ENGINE_OPTIONS = {}
    REDIS_URL = os.environ.get("NASCENT_TEST_REDIS_URL")
    TASKS_INLINE = True
