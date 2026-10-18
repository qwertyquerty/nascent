import hashlib
import json
import re
from dataclasses import dataclass
from datetime import datetime, timedelta
from typing import Any

import nascent_core as nc
from flask import current_app
from sqlalchemy import func, select, update
from sqlalchemy.exc import IntegrityError

from .. import tasks, timeutil
from ..errors import ApiError
from ..extensions import db
from ..models import COUNTED_PLAY_STATUSES, Chart, ChartStatus, Play, PlayStatus, PlayTicket, Role, User
from ..ratelimit import check_rate_limit
from ..security import TICKET_PREFIX, hash_token, new_token, require_not_banned
from ..storage import put_json_gz, sharded_key
from . import anticheat, ranking
from .calculator import calculator_version, performance, require_rate_key
from .charts import MAX_DURATION_MS, chart_difficulty, get_chart, native_chart, parse_chart, register_chart

INT32_MIN = -(2**31)
INT32_MAX = 2**31 - 1
ACCURACY_TOLERANCE = 1e-9
REPLAY_TEXT_LIMIT = 256
REPLAY_PATH_LIMIT = 512
DIAGNOSTICS_MAX_FIELDS = 64
DIAGNOSTICS_KEY_LIMIT = 64
MAX_REPLAY_PAUSES = 1000
REPLAY_CHART_FIELDS = ("path", "gameplay_hash", "title", "artist", "difficulty_name", "key_count")
REPLAY_SETTINGS_FIELDS = ("audio_input_offset", "audio_visual_offset", "scroll_speed", "mirror")
REPLAY_RESULT_FIELDS = ("accuracy", "grade", "max_combo", "score_counts", "average_error", "difficulty", "performance")
PATH_SEPARATORS = re.compile(r"[\\/]+")
WINDOWS_DRIVE = re.compile(r"^[A-Za-z]:")


@dataclass
class IssuedTicket:
    token: str
    ticket: PlayTicket
    chart: Chart | None


@dataclass
class Standing:
    key_count: int
    previous_performance: float
    previous_rank: int | None
    performance: float = 0.0
    rank: int | None = None


@dataclass
class SubmissionResult:
    play: Play
    chart: Chart
    chart_created: bool
    chart_rank: int | None
    key_mode: Standing | None
    previous_best_performance: float | None = None


def score_counts(simulation: nc.Simulation) -> dict[str, int]:
    return {name: int(simulation.score_counts.get(name, 0)) for name in nc.HIT_SCORES}


def judgement_fields(simulation: nc.Simulation) -> dict[str, Any]:
    counts = score_counts(simulation)
    fields: dict[str, Any] = {column: counts[name] for name, column in Play.SCORE_COLUMNS.items()}
    fields.update(
        accuracy=simulation.accuracy,
        grade=simulation.grade,
        max_combo=simulation.max_combo,
        average_error=simulation.average_error,
        hit_count=sum(count for name, count in counts.items() if name != "MISS"),
    )
    return fields


def issue_ticket(user: User, chart_hash: str, rate: float) -> IssuedTicket:
    require_not_banned(user)
    check_rate_limit("ticket", user.id)
    rate_key = require_rate_key(rate)
    chart = get_chart(chart_hash)
    now = timeutil.utcnow()
    longest_ms = chart.duration_ms if chart is not None else MAX_DURATION_MS
    lifetime = timedelta(milliseconds=longest_ms / (rate_key / 100)) + current_app.config["TICKET_GRACE"]

    db.session.execute(select(User.id).where(User.id == user.id).with_for_update())
    db.session.execute(
        update(PlayTicket)
        .where(PlayTicket.user_id == user.id, PlayTicket.used_at.is_(None), PlayTicket.superseded_at.is_(None), PlayTicket.expires_at > now)
        .values(superseded_at=now)
    )

    token = new_token(TICKET_PREFIX)
    ticket = PlayTicket(
        token_hash=hash_token(token), user_id=user.id, chart_hash=chart_hash.lower(), rate_key=rate_key, issued_at=now, expires_at=now + lifetime
    )
    db.session.add(ticket)
    db.session.commit()
    return IssuedTicket(token, ticket, chart)


def _invalid_replay(message: str, details: Any = None) -> ApiError:
    return ApiError(422, "invalid_replay", message, details)


def _validate_raw_inputs(raw_inputs: Any, key_count: int, note_count: int) -> None:
    config = current_app.config

    if not isinstance(raw_inputs, list):
        raise _invalid_replay("inputs must be a list")

    if len(raw_inputs) > note_count * config["MAX_REPLAY_INPUTS_PER_NOTE"] + config["MAX_REPLAY_EXTRA_INPUTS"]:
        raise _invalid_replay("The replay has far more inputs than the chart has notes")

    for index, entry in enumerate(raw_inputs):
        if not isinstance(entry, list) or len(entry) != 3 or not all(type(value) is int for value in entry):
            raise _invalid_replay(f"input {index} must be [time, lane, pressed] integers")

        time, lane, pressed = entry

        if not INT32_MIN <= time <= INT32_MAX or not 0 <= lane < key_count or pressed not in (0, 1):
            raise _invalid_replay(f"input {index} is out of range")


def _short(value: Any, limit: int = REPLAY_TEXT_LIMIT) -> Any:
    return value[:limit] if isinstance(value, str) else value


def _pick(value: Any, fields: tuple[str, ...]) -> Any:
    if not isinstance(value, dict):
        return value

    return {field: _short(value[field]) for field in fields if field in value}


def public_chart_path(path: Any) -> Any:
    if not isinstance(path, str):
        return path

    parts = [part for part in PATH_SEPARATORS.split(path) if part]

    if path.startswith(("/", "\\", "~")) or WINDOWS_DRIVE.match(path) or ".." in parts:
        return parts[-1][:REPLAY_PATH_LIMIT] if parts else ""

    return path[:REPLAY_PATH_LIMIT]


def _diagnostics(value: Any) -> dict | None:
    if not isinstance(value, dict):
        return None

    kept = {}

    for key, item in value.items():
        if len(kept) >= DIAGNOSTICS_MAX_FIELDS:
            break

        if len(key) <= DIAGNOSTICS_KEY_LIMIT and (item is None or isinstance(item, (bool, int, float, str))):
            kept[key] = _short(item)

    return kept


def _is_number(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def storable_replay(raw: dict) -> dict:
    replay = {key: raw[key] for key in ("version", "rate", "end_time", "inputs") if key in raw}

    if "played_at" in raw:
        replay["played_at"] = _short(raw["played_at"])

    if "chart" in raw:
        chart = _pick(raw["chart"], REPLAY_CHART_FIELDS)

        if isinstance(chart, dict) and "path" in chart:
            chart["path"] = public_chart_path(raw["chart"]["path"])

        replay["chart"] = chart

    if "settings" in raw:
        replay["settings"] = _pick(raw["settings"], REPLAY_SETTINGS_FIELDS)

    if "result" in raw:
        result = _pick(raw["result"], REPLAY_RESULT_FIELDS)

        if isinstance(result, dict) and isinstance(result.get("score_counts"), dict):
            result["score_counts"] = {name: count for name, count in result["score_counts"].items() if name in nc.HIT_SCORES}

        replay["result"] = result

    diagnostics = _diagnostics(raw.get("diagnostics"))

    if diagnostics is not None:
        replay["diagnostics"] = diagnostics

    input_times = raw.get("input_times")
    inputs = raw.get("inputs")

    if isinstance(input_times, list) and isinstance(inputs, list) and len(input_times) == len(inputs) and all(_is_number(time) for time in input_times):
        replay["input_times"] = input_times

    clock_samples = raw.get("clock_samples")

    if isinstance(clock_samples, list):
        limit = current_app.config["ANTICHEAT_CLOCK_MAX_SAMPLES"]
        replay["clock_samples"] = [sample[:2] for sample in clock_samples[:limit] if isinstance(sample, list)]

    pauses = raw.get("pauses")

    if isinstance(pauses, list):
        replay["pauses"] = [pause[:3] for pause in pauses[:MAX_REPLAY_PAUSES] if isinstance(pause, list)]

    return replay


def _validate_replay(replay: nc.Replay, native: nc.Chart) -> None:
    config = current_app.config

    if replay.version != nc.REPLAY_FORMAT_VERSION:
        raise _invalid_replay(f"Replay format version {replay.version} is not supported", {"supported": nc.REPLAY_FORMAT_VERSION})

    if replay.key_count != native.key_count:
        raise _invalid_replay("The replay's key count does not match the chart")

    chart_end = native.end_ms_at(replay.rate)

    if replay.end_time < chart_end:
        raise ApiError(422, "incomplete_play", "Only plays that reach the end of the chart can be submitted")

    if replay.end_time > chart_end + config["MAX_END_OVERRUN_MS"]:
        raise _invalid_replay("The replay ends long after the chart does")

    latest = None

    for time, _, _ in replay.inputs:
        if time < -config["MAX_LEAD_IN_MS"] or time > replay.end_time:
            raise _invalid_replay("An input lies outside the play")

        if latest is not None and time < latest - config["MAX_INPUT_BACKSTEP_MS"]:
            raise _invalid_replay("Inputs go back in time")

        latest = time if latest is None else max(latest, time)


def _check_claimed_result(replay: nc.Replay, simulation: nc.Simulation) -> None:
    claimed = replay.claimed_result
    claimed_counts = {name: int(claimed["score_counts"].get(name, 0)) for name in nc.HIT_SCORES}
    server_counts = score_counts(simulation)

    if abs(claimed["accuracy"] - simulation.accuracy) > ACCURACY_TOLERANCE or claimed["max_combo"] != simulation.max_combo or claimed_counts != server_counts:
        raise ApiError(
            422,
            "result_mismatch",
            "The submitted result does not match the server's judgement of the replay",
            {
                "server": {"accuracy": simulation.accuracy, "max_combo": simulation.max_combo, "score_counts": server_counts},
                "client": {"accuracy": claimed["accuracy"], "max_combo": claimed["max_combo"], "score_counts": claimed_counts},
            },
        )


def replay_fingerprint(chart_hash: str, replay: nc.Replay) -> str:
    payload = json.dumps([chart_hash, replay.rate, replay.end_time, replay.inputs], separators=(",", ":"))
    return hashlib.sha256(payload.encode()).hexdigest()


def _resolve_ticket(
    user: User, token: str | None, chart_hash: str, rate_key: int, playable_ms: int, chart_length_ms: int
) -> tuple[PlayTicket | None, anticheat.Flag | None]:
    if token is None:
        return None, None

    now = timeutil.utcnow()
    ticket = db.session.scalar(select(PlayTicket).where(PlayTicket.token_hash == hash_token(token)).with_for_update())

    if ticket is None or ticket.user_id != user.id:
        return None, anticheat.Flag("ticket_rejected", anticheat.INFO, "The play ticket does not exist", {"reason": "unknown"})

    if ticket.used_at is not None:
        return None, anticheat.Flag("ticket_rejected", anticheat.INFO, "The play ticket was already used", {"reason": "used"})

    if ticket.superseded_at is not None:
        return None, anticheat.Flag("ticket_rejected", anticheat.INFO, "A newer play ticket replaced this one", {"reason": "superseded"})

    if ticket.chart_hash != chart_hash or ticket.rate_key != rate_key:
        return None, anticheat.Flag("ticket_rejected", anticheat.INFO, "The play ticket was issued for a different chart or rate", {"reason": "mismatch"})

    expires_at = min(ticket.expires_at, ticket.issued_at + timedelta(milliseconds=chart_length_ms) + current_app.config["TICKET_GRACE"])

    if expires_at <= now:
        return None, anticheat.Flag("ticket_rejected", anticheat.INFO, "The play ticket expired", {"reason": "expired"})

    elapsed_ms = (now - ticket.issued_at).total_seconds() * 1000
    required_ms = max(playable_ms, 0) * current_app.config["TICKET_MIN_ELAPSED_RATIO"]

    if elapsed_ms < required_ms:
        raise ApiError(
            422,
            "play_too_fast",
            "The play was submitted sooner than it could have been played",
            {"elapsed_ms": int(elapsed_ms), "required_ms": int(required_ms)},
        )

    ticket.used_at = now
    return ticket, None


def _player_history(user: User) -> anticheat.PlayerHistory:
    cutoff = timeutil.utcnow() - timedelta(hours=current_app.config["ANTICHEAT_SPIKE_BASELINE_HOURS"])
    ranked = (Play.user_id == user.id, Play.ranked.is_(True))
    count, best = db.session.execute(
        select(func.count(Play.id), func.max(Play.performance)).where(*ranked, Play.status == PlayStatus.VERIFIED, Play.submitted_at < cutoff)
    ).one()
    recent = db.session.scalars(select(Play.performance).where(*ranked, Play.status.in_(COUNTED_PLAY_STATUSES), Play.submitted_at >= cutoff)).all()
    return anticheat.PlayerHistory(int(count or 0), float(best or 0.0), [float(value) for value in recent])


def _fingerprint_history(user: User) -> list[dict]:
    rows = db.session.scalars(
        select(Play.fingerprint)
        .where(Play.user_id == user.id, Play.status == PlayStatus.VERIFIED, Play.fingerprint.is_not(None))
        .order_by(Play.submitted_at.desc())
        .limit(current_app.config["ANTICHEAT_FINGERPRINT_HISTORY"])
    ).all()
    return [row for row in rows if isinstance(row, dict)]


def submit_play(
    user: User,
    chart_hash: str,
    raw_replay: dict,
    raw_chart: dict | None,
    ticket_token: str | None,
    *,
    imported: bool = False,
    trusted: bool = False,
    submitted_at: datetime | None = None,
) -> SubmissionResult:
    require_not_banned(user)
    chart_hash = chart_hash.lower()
    chart = get_chart(chart_hash)

    if chart is None:
        if raw_chart is None:
            raise ApiError(409, "chart_required", "The server does not have this chart yet, submit it with the play", {"chart_hash": chart_hash})

        check_rate_limit("chart_upload", user.id)
        native = parse_chart(raw_chart)

        if native.hash != chart_hash:
            raise ApiError(400, "chart_hash_mismatch", "The uploaded chart does not match chart_hash", {"expected": chart_hash, "actual": native.hash})
    else:
        native = native_chart(chart)

    _validate_raw_inputs(raw_replay.get("inputs"), native.key_count, native.note_count)

    try:
        replay = nc.Replay.from_json(json.dumps(storable_replay(raw_replay)))
    except ValueError as error:
        raise _invalid_replay(f"The replay could not be read: {error}") from None

    rate_key = require_rate_key(replay.rate)
    _validate_replay(replay, native)
    simulation = nc.simulate(native, replay)
    _check_claimed_result(replay, simulation)

    fingerprint = replay_fingerprint(chart_hash, replay)

    if db.session.scalar(select(Play.id).where(Play.replay_hash == fingerprint)) is not None:
        raise ApiError(409, "duplicate_replay", "This replay has already been submitted")

    playable_ms = replay.end_time - native.intro_skip_ms_at(replay.rate)
    chart_length_ms = native.duration_ms_at(replay.rate)
    ticket, ticket_flag = (None, None) if trusted else _resolve_ticket(user, ticket_token, chart_hash, rate_key, playable_ms, chart_length_ms)
    eligible = trusted or ticket is not None
    chart_created = False

    if chart is None:
        chart, chart_created = register_chart(native, user, (rate_key,))

    difficulty = chart_difficulty(chart, rate_key)
    play_performance = performance(difficulty, simulation.accuracy)
    flags = anticheat.timing_flags(simulation, replay.inputs, current_app.config)

    play_fingerprint = anticheat.fingerprint(simulation, replay.inputs)

    if not imported and not trusted:
        flags.extend(anticheat.build_flags(raw_replay.get("diagnostics"), user.has_role(Role.MODERATOR), current_app.config))
        flags.extend(anticheat.clock_flags(raw_replay.get("clock_samples"), current_app.config))

        if play_fingerprint is not None:
            flags.extend(anticheat.fingerprint_flags(play_fingerprint, _fingerprint_history(user), current_app.config))

    if ticket_flag is not None:
        flags.append(ticket_flag)

    if eligible and chart.status == ChartStatus.RANKED:
        flags.extend(anticheat.performance_flags(play_performance, _player_history(user), current_app.config))

    status = PlayStatus.PENDING_REVIEW if eligible and anticheat.needs_review(flags) else PlayStatus.VERIFIED

    replay_key = sharded_key("replays", fingerprint, ".json.gz")
    key_count = native.key_count
    key_mode = (
        Standing(key_count, ranking.standing(user, key_count)[0], ranking.global_rank(user, key_count)) if key_count in ranking.ranked_key_counts() else None
    )

    db.session.execute(select(User).where(User.id == user.id).with_for_update()).scalar_one()

    previous_best_performance = db.session.scalar(
        select(func.max(Play.performance)).where(Play.user_id == user.id, Play.chart_id == chart.id, Play.status.in_(COUNTED_PLAY_STATUSES))
    )

    play = Play(
        user_id=user.id,
        chart_id=chart.id,
        rate_key=rate_key,
        key_count=native.key_count,
        status=status,
        eligible=eligible,
        ranked=eligible and status in COUNTED_PLAY_STATUSES and chart.status == ChartStatus.RANKED and is_rankable(chart_length_ms, chart.note_count),
        **judgement_fields(simulation),
        duration_ms=chart_length_ms,
        difficulty=difficulty,
        performance=play_performance,
        calculator_version=calculator_version(),
        replay_hash=fingerprint,
        replay_key=replay_key,
        settings=replay.settings,
        flags=[flag.to_dict() for flag in flags],
        fingerprint=play_fingerprint,
        submitted_at=submitted_at or timeutil.utcnow(),
        ticket_id=ticket.id if ticket else None,
    )
    db.session.add(play)

    try:
        db.session.flush()
    except IntegrityError:
        db.session.rollback()
        raise ApiError(409, "duplicate_replay", "This replay has already been submitted") from None

    put_json_gz(replay_key, replay.to_json())
    db.session.execute(update(Chart).where(Chart.id == chart.id).values(play_count=Chart.play_count + 1))
    ranking.apply_play_change(user, chart.id, rate_key)
    db.session.commit()

    ranking.sync_leaderboard(user)

    if chart_created:
        tasks.enqueue(tasks.fill_chart_difficulties, chart.id)

    db.session.refresh(play)
    db.session.refresh(chart)

    if key_mode is not None:
        key_mode.performance = ranking.standing(user, key_count)[0]
        key_mode.rank = ranking.global_rank(user, key_count)

        if play.ranked:
            ranking.record_snapshot(user.id, key_count, key_mode.performance, key_mode.rank)
            db.session.commit()

    return SubmissionResult(
        play=play,
        chart=chart,
        chart_created=chart_created,
        chart_rank=chart_rank(play),
        key_mode=key_mode,
        previous_best_performance=float(previous_best_performance) if previous_best_performance is not None else None,
    )


def chart_rank(play: Play) -> int | None:
    if not play.is_chart_best:
        return None

    better = db.session.scalar(
        select(func.count())
        .select_from(Play)
        .join(User, User.id == Play.user_id)
        .where(
            Play.chart_id == play.chart_id,
            Play.rate_key == play.rate_key,
            Play.is_chart_best.is_(True),
            User.is_banned.is_(False),
            (Play.accuracy > play.accuracy)
            | ((Play.accuracy == play.accuracy) & (Play.max_combo > play.max_combo))
            | ((Play.accuracy == play.accuracy) & (Play.max_combo == play.max_combo) & (Play.id < play.id)),
        )
    )
    return int(better or 0) + 1


def chart_board_size(chart_id: int, rate_key: int) -> int:
    return int(
        db.session.scalar(
            select(func.count())
            .select_from(Play)
            .join(User, User.id == Play.user_id)
            .where(Play.chart_id == chart_id, Play.rate_key == rate_key, Play.is_chart_best.is_(True), User.is_banned.is_(False))
        )
        or 0
    )


def chart_performance_rank(play: Play) -> int | None:
    if not play.is_profile_best:
        return None

    better = db.session.scalar(
        select(func.count())
        .select_from(Play)
        .join(User, User.id == Play.user_id)
        .where(
            Play.chart_id == play.chart_id,
            Play.is_profile_best.is_(True),
            User.is_banned.is_(False),
            (Play.performance > play.performance)
            | ((Play.performance == play.performance) & (Play.accuracy > play.accuracy))
            | ((Play.performance == play.performance) & (Play.accuracy == play.accuracy) & (Play.id < play.id)),
        )
    )
    return int(better or 0) + 1


def is_rankable(duration_ms: int, note_count: int) -> bool:
    return duration_ms >= current_app.config["RANKED_MIN_DURATION_MS"] and note_count >= current_app.config["RANKED_MIN_NOTES"]


def review_play(play: Play, moderator: User | None, approve: bool, note: str) -> None:
    play.status = PlayStatus.VERIFIED if approve else PlayStatus.REJECTED
    play.eligible = play.eligible or approve
    play.ranked = approve and play.chart.status == ChartStatus.RANKED and is_rankable(play.duration_ms, play.chart.note_count)
    play.reviewed_by_id = moderator.id if moderator is not None else None
    play.reviewed_at = timeutil.utcnow()
    play.review_note = note[:500]
    db.session.flush()
    ranking.apply_play_change(play.user, play.chart_id, play.rate_key)
