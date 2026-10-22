import itertools
import json
import multiprocessing
import os
import sys
import time
from collections import Counter, defaultdict, deque
from collections.abc import Iterator
from concurrent.futures import ProcessPoolExecutor
from datetime import UTC, datetime
from pathlib import Path
from typing import NamedTuple

import click
import flask_migrate
import nascent_core as nc
from flask import current_app
from flask.cli import AppGroup
from sqlalchemy import delete, func, inspect, or_, select

from . import tasks, timeutil
from .errors import ApiError
from .extensions import db
from .models import AuthSession, Chart, ChartDifficulty, ChartSource, ChartStatus, PasswordReset, Play, PlayTicket, Role, User
from .security import hash_password, revoke_user_sessions
from .services import ranking
from .services.calculator import calculator_version
from .services.charts import add_chart_source, get_chart, osu_source, parse_chart, read_osu_file, register_chart, remove_chart, replace_chart, set_chart_status
from .services.plays import review_play, submit_play
from .storage import storage

nascent_cli = AppGroup("nascent", help="Nascent server administration")

CHART_SUFFIXES = (".osu", ".json", ".ssc", ".sm", ".dwi")
STEPMANIA_PREFERENCE = {".ssc": 3, ".sm": 2, ".dwi": 1}
ETTERNA_SOURCE = "etterna"
PACK_INFO = "pack.json"
PARALLEL_MIN_FILES = 64
PARALLEL_MAX_WORKERS = 16
PARALLEL_QUEUE_PER_WORKER = 4
BASELINE_REVISION = "0001"


def _report(job: str | None, done: str) -> None:
    click.echo(f"queued job {job}" if job else done)


@nascent_cli.command("upgrade-db")
def upgrade_db():
    tables = set(inspect(db.engine).get_table_names())

    if "alembic_version" not in tables and "users" in tables:
        flask_migrate.stamp(revision=BASELINE_REVISION)
        click.echo(f"marked the existing database as revision {BASELINE_REVISION}")

    flask_migrate.upgrade()
    click.echo("the database schema is up to date")


@nascent_cli.command("create-user")
@click.argument("username")
@click.argument("email")
@click.option("--role", type=click.Choice([role.value for role in Role]), default=Role.USER.value)
@click.password_option()
def create_user(username: str, email: str, role: str, password: str):
    user = User(username=username, username_key=username.lower(), email=email, email_key=email.lower(), password_hash=hash_password(password), role=role)
    db.session.add(user)
    db.session.commit()
    click.echo(f"created {role} {username} with id {user.id}")


def _get_user(username: str) -> User:
    user = db.session.scalar(select(User).where(User.username_key == username.lower()))

    if user is None:
        raise click.ClickException(f"no user named {username}")

    return user


@nascent_cli.command("set-role")
@click.argument("username")
@click.argument("role", type=click.Choice([role.value for role in Role]))
def set_role(username: str, role: str):
    user = _get_user(username)
    user.role = role
    db.session.commit()
    click.echo(f"{username} is now {role}")


@nascent_cli.command("set-password")
@click.argument("username")
@click.option("--password-stdin", is_flag=True, help="Read the new password from standard input instead of prompting")
def set_password(username: str, password_stdin: bool):
    user = _get_user(username)
    password = sys.stdin.readline().rstrip("\r\n") if password_stdin else click.prompt("New password", hide_input=True, confirmation_prompt=True)

    if not 8 <= len(password) <= 256:
        raise click.ClickException("the password must be 8 to 256 characters long")

    user.password_hash = hash_password(password)
    revoke_user_sessions(user.id)
    db.session.commit()
    click.echo(f"set the password for {user.username} and signed them out everywhere")


def _read_replay(file: Path) -> dict | None:
    try:
        raw = json.loads(file.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None

    return raw if isinstance(raw, dict) and isinstance(raw.get("chart"), dict) and isinstance(raw.get("inputs"), list) else None


def _played_at(raw: dict) -> datetime | None:
    try:
        return datetime.fromisoformat(str(raw.get("played_at"))).astimezone(UTC)
    except ValueError:
        return None


def _replay_charts(raw: dict) -> list[Chart]:
    meta = raw["chart"]
    gameplay_hash = meta.get("gameplay_hash")

    if isinstance(gameplay_hash, str) and (chart := get_chart(gameplay_hash)) is not None:
        return [chart]

    return list(
        db.session.scalars(
            select(Chart).where(
                func.lower(Chart.title) == str(meta.get("title", "")).lower(),
                func.lower(Chart.artist) == str(meta.get("artist", "")).lower(),
                func.lower(Chart.difficulty_name) == str(meta.get("difficulty_name", "")).lower(),
                Chart.key_count == meta.get("key_count"),
            )
        )
    )


def _import_replay(user: User, raw: dict) -> tuple[str, str]:
    charts = _replay_charts(raw)

    if not charts:
        return "skipped", "its chart is not on the server"

    reason = "it does not match any chart on the server with that name"

    for chart in charts:
        name = f"{chart.artist} - {chart.title} [{chart.difficulty_name}]"

        try:
            result = submit_play(user, chart.hash, raw, None, None, imported=True, trusted=True, submitted_at=_played_at(raw))
        except ApiError as error:
            db.session.rollback()

            if error.code == "duplicate_replay":
                return "exists", name

            if error.code not in ("result_mismatch", "invalid_replay"):
                reason = error.message

            continue

        play = result.play
        return "added", f"{name} {play.rate:.2f}x {play.accuracy * 100:.2f}% {'ranked' if play.ranked else play.status}"

    return "skipped", reason


@nascent_cli.command("import-replays")
@click.argument("path", type=click.Path(exists=True, path_type=Path))
@click.option("--user", "username", required=True, help="The player the replays belong to")
def import_replays(path: Path, username: str):
    user = _get_user(username)
    files = [path] if path.is_file() else sorted(path.rglob("*.json"))
    replays = [(file, raw) for file in files if (raw := _read_replay(file)) is not None]
    counts = Counter()

    if not replays:
        raise click.ClickException(f"no replay files in {path} that this user can read")

    replays.sort(key=lambda entry: str(entry[1].get("played_at", "")))

    for file, raw in replays:
        outcome, detail = _import_replay(user, raw)
        counts[outcome] += 1
        click.echo(f"{outcome} {file.name}: {detail}")

    click.echo(f"{counts['added']} added, {counts['exists']} already on the server, {counts['skipped']} skipped, {len(files) - len(replays)} not replays")


def _chart_files(path: Path) -> list[Path]:
    if path.is_file():
        return [path]

    files = sorted(file for file in path.rglob("*") if file.suffix.lower() in CHART_SUFFIXES and file.name != PACK_INFO and file.is_file())
    best: dict[Path, int] = {}

    for file in files:
        best[file.parent] = max(best.get(file.parent, 0), STEPMANIA_PREFERENCE.get(file.suffix.lower(), 0))

    return [file for file in files if STEPMANIA_PREFERENCE.get(file.suffix.lower(), best[file.parent]) >= best[file.parent]]


def _etterna_pack(file: Path, root: Path, cache: dict[Path, int | None]) -> int | None:
    for folder in file.parents:
        if folder not in cache:
            info = folder / PACK_INFO
            pack = None

            if info.is_file():
                try:
                    data = json.loads(info.read_text(encoding="utf-8"))
                    pack = int(data["id"]) if data.get("site") == "etternaonline" else None
                except (ValueError, KeyError, TypeError):
                    pack = None

            cache[folder] = pack

        if cache[folder] is not None or folder == root or folder == folder.parent:
            return cache[folder]

    return None


def _read_chart_file(file: Path, pack: int | None) -> list[tuple[dict | None, dict | None, str | None, bool]]:
    suffix = file.suffix.lower()

    if suffix == ".osu":
        raw = file.read_bytes()

        try:
            data, source, modified = read_osu_file(raw)
        except ValueError as error:
            return [(None, osu_source(raw), str(error), False)]

        return [(data, source, None, modified)]

    if suffix in STEPMANIA_PREFERENCE:
        charts = []

        for entry in nc.convert_stepmania(file.read_bytes()):
            source = {"source": ETTERNA_SOURCE, "checksum": entry["chart_key"], "source_set_id": pack} if entry["chart_key"] else None

            if entry["chart"] is None:
                charts.append((None, source, entry["error"], False))
                continue

            charts.append((json.loads(entry["chart"]), source, None, bool(entry.get("modified"))))

        return charts

    return [(json.loads(file.read_text(encoding="utf-8")), None, None, False)]


class _Converted(NamedTuple):
    index: int
    chart_hash: str | None
    key_count: int | None
    source: dict | None
    problem: str | None
    modified: bool


def _convert_file(job: tuple[str, int | None]) -> list[_Converted]:
    try:
        entries = _read_chart_file(Path(job[0]), job[1])
    except (OSError, ValueError) as error:
        return [_Converted(0, None, None, None, str(error), False)]

    converted = []

    for index, (data, source, problem, modified) in enumerate(entries):
        if data is not None:
            try:
                native = parse_chart(data)
            except (ApiError, ValueError) as error:
                problem = str(getattr(error, "message", error))
            else:
                converted.append(_Converted(index, native.hash, native.key_count, source, None, modified))
                continue

        converted.append(_Converted(index, None, None, source, problem, False))

    return converted


def _converted_files(jobs: list[tuple[str, int | None]], workers: int) -> Iterator[tuple[tuple[str, int | None], list[_Converted]]]:
    if workers <= 1 or len(jobs) < PARALLEL_MIN_FILES:
        for job in jobs:
            yield job, _convert_file(job)

        return

    with ProcessPoolExecutor(max_workers=workers, mp_context=multiprocessing.get_context("spawn")) as pool:
        remaining = iter(jobs)
        pending = deque((job, pool.submit(_convert_file, job)) for job in itertools.islice(remaining, workers * PARALLEL_QUEUE_PER_WORKER))

        while pending:
            job, future = pending.popleft()
            following = next(remaining, None)

            if following is not None:
                pending.append((following, pool.submit(_convert_file, following)))

            yield job, future.result()


def _source_key(source: dict | None) -> tuple | None:
    return (source["source"], source["checksum"], source.get("source_set_id")) if source is not None else None


@nascent_cli.command("import-charts")
@click.argument("path", type=click.Path(exists=True, path_type=Path))
@click.option("--rank", is_flag=True, help="Rank every imported chart")
@click.option("--quiet", is_flag=True, help="Print progress every 1000 files and a summary instead of a line per chart")
@click.option(
    "--replace",
    is_flag=True,
    help="Replace charts from the same file and pack that import differently now, moving their plays onto the new version and re-judging them, and remove charts that are no longer valid",
)
@click.option("--dry-run", is_flag=True, help="Report what would be added and replaced without changing anything")
@click.option("--workers", type=int, default=0, help="Processes that read and convert chart files at once, 0 for one per CPU")
def import_charts(path: Path, rank: bool, quiet: bool, replace: bool, dry_run: bool, workers: int):
    files = _chart_files(path)
    packs: dict[Path, int | None] = {}
    counts = Counter()
    reasons = Counter()
    produced: dict[tuple, dict[str, int]] = defaultdict(dict)
    invalid: dict[tuple, str] = {}
    previous: dict[tuple, tuple[int, str]] = {}
    hashes: set[str] = set()

    if not files:
        raise click.ClickException(f"no {', '.join(CHART_SUFFIXES)} chart files in {path} that this user can read")

    jobs = [(str(file), _etterna_pack(file, path, packs)) for file in files]
    workers = workers if workers > 0 else min(os.cpu_count() or 1, PARALLEL_MAX_WORKERS)

    for position, ((file, pack), converted) in enumerate(_converted_files(jobs, workers), 1):
        loaded = None

        for entry in converted:
            key = _source_key(entry.source)

            if replace and key is not None and key not in previous:
                prior = _imported_version(entry.source)

                if prior is not None:
                    previous[key] = (prior.id, prior.hash)

            try:
                if entry.problem is not None:
                    if key is not None:
                        invalid.setdefault(key, entry.problem)

                    raise ValueError(entry.problem)

                hashes.add(entry.chart_hash)

                if key is not None:
                    produced[key][entry.chart_hash] = entry.key_count

                chart = get_chart(entry.chart_hash)
                created = chart is None

                if dry_run:
                    db.session.rollback()
                    counts["added" if created else "exists"] += 1
                    continue

                if created:
                    loaded = loaded if loaded is not None else _read_chart_file(Path(file), pack)
                    chart, created = register_chart(parse_chart(loaded[entry.index][0]), None, modified=entry.modified)
                else:
                    chart.modified = chart.modified or entry.modified

                if entry.source is not None:
                    add_chart_source(chart, **entry.source)

                changed = set_chart_status(chart, ChartStatus.RANKED, None) if rank and _meets_ranked_minimums(chart) else False
            except (ApiError, ValueError) as error:
                db.session.rollback()
                message = str(getattr(error, "message", error))
                counts["skipped"] += 1
                reasons[message] += 1

                if not quiet:
                    click.echo(f"skipped {file}: {message}")

                continue

            db.session.commit()

            if created:
                tasks.enqueue(tasks.fill_chart_difficulties, chart.id)

            if changed:
                tasks.enqueue(tasks.apply_chart_status, chart.id)

            counts["added" if created else "exists"] += 1

            if not quiet:
                name = f"{chart.artist} - {chart.title} [{chart.difficulty_name}]"
                click.echo(f"{'added' if created else 'exists'} {chart.hash[:16]} {chart.key_count:2}K {chart.status:8} {chart.difficulty:7.2f} {name}")

        if quiet and position % 1000 == 0:
            click.echo(f"{position}/{len(files)} files: {counts['added']} added, {counts['exists']} already on the server, {counts['skipped']} skipped")

    if replace:
        _apply_replacements(previous, produced, invalid, hashes, dry_run, counts)

    if quiet and reasons:
        for message, count in reasons.most_common(15):
            click.echo(f"skipped {count}: {message}")

    summary = f"{counts['added']} new charts, {counts['exists']} already on the server, {counts['skipped']} skipped, from {len(files)} files"

    if replace:
        summary += f", {'would replace' if dry_run else 'replaced'} {counts['replaced']} older versions"
        summary += f", {'would remove' if dry_run else 'removed'} {counts['removed']} invalid charts"

    click.echo(("Dry run, nothing changed: " if dry_run else "") + summary)


def _apply_replacements(previous: dict[tuple, tuple[int, str]], produced: dict[tuple, dict[str, int]], invalid: dict[tuple, str], hashes: set[str], dry_run: bool, counts: Counter) -> None:
    for key, (chart_id, chart_hash) in previous.items():
        if chart_hash in hashes:
            continue

        old = db.session.get(Chart, chart_id)

        if old is None:
            continue

        name = f"{old.artist} - {old.title} [{old.difficulty_name}]"
        versions = produced.get(key, {})

        if len(versions) > 1:
            click.echo(f"not replacing {chart_hash[:16]}: {len(versions)} different charts in this import share its source, so it's unclear which one replaces it: {name}")
            continue

        if not versions:
            if key in invalid:
                _remove_invalid(old, invalid[key], dry_run)
                counts["removed"] += 1

            continue

        new_hash, key_count = next(iter(versions.items()))

        if key_count != old.key_count:
            click.echo(f"not replacing {chart_hash[:16]}: the new version has {key_count} keys instead of {old.key_count}: {name}")
            continue

        counts["replaced"] += 1

        if dry_run:
            click.echo(f"would replace {chart_hash[:16]} ({old.play_count} plays) with {new_hash[:16]}: {name}")
            db.session.rollback()
            continue

        new = get_chart(new_hash)
        replaced = replace_chart(old, new)
        db.session.commit()

        for file in replaced.files:
            try:
                storage().delete(file)
            except OSError:
                current_app.logger.warning("could not delete %s for replaced chart %s", file, replaced.old_hash)

        tasks.enqueue_chart_replacement(new.id, replaced.user_ids)
        click.echo(f"replaced {replaced.old_hash[:16]} with {new.hash[:16]}, moved {len(replaced.user_ids)} players' plays: {name}")


def _imported_version(source: dict | None) -> Chart | None:
    if source is None:
        return None

    existing = db.session.scalar(select(ChartSource).where(ChartSource.source == source["source"], ChartSource.checksum == source["checksum"]))

    if existing is None or existing.source_set_id != source.get("source_set_id"):
        return None

    return existing.chart


def _remove_invalid(chart: Chart, problem: str | None, dry_run: bool) -> None:
    name = f"{chart.artist} - {chart.title} [{chart.difficulty_name}]"

    if dry_run:
        click.echo(f"would remove {chart.hash[:16]} ({chart.play_count} plays), it's no longer valid: {problem}: {name}")
        db.session.rollback()
        return

    removal = remove_chart(chart)
    db.session.commit()

    for key in removal.files:
        try:
            storage().delete(key)
        except OSError:
            current_app.logger.warning("could not delete %s for removed chart %s", key, removal.old_hash)

    tasks.enqueue_user_refresh(removal.user_ids, [])
    click.echo(f"removed {removal.old_hash[:16]} and its plays from {len(removal.user_ids)} players, it's no longer valid: {problem}: {name}")


@nascent_cli.command("set-chart-status")
@click.argument("chart_hash")
@click.argument("status", type=click.Choice([status.value for status in ChartStatus]))
def chart_status(chart_hash: str, status: str):
    chart = get_chart(chart_hash)

    if chart is None:
        raise click.ClickException("no chart with that hash")

    try:
        changed = set_chart_status(chart, ChartStatus(status), None)
    except ApiError as error:
        raise click.ClickException(error.message) from None

    db.session.commit()

    if not changed:
        click.echo(f"{chart.title} is already {status}")
        return

    _report(tasks.enqueue(tasks.apply_chart_status, chart.id), f"{chart.title} is now {status}")


def _meets_ranked_minimums(chart: Chart) -> bool:
    return chart.note_count >= current_app.config["RANKED_MIN_NOTES"] and chart.duration_ms >= current_app.config["RANKED_MIN_DURATION_MS"]


@nascent_cli.command("unrank-short-charts")
@click.option("--dry-run", is_flag=True, help="List the charts without unranking them")
def unrank_short_charts(dry_run: bool):
    minimum_notes = current_app.config["RANKED_MIN_NOTES"]
    minimum_ms = current_app.config["RANKED_MIN_DURATION_MS"]
    query = select(Chart).where(Chart.status == ChartStatus.RANKED, or_(Chart.note_count < minimum_notes, Chart.duration_ms < minimum_ms)).order_by(Chart.id)
    charts = db.session.scalars(query).all()

    for chart in charts:
        name = f"{chart.artist} - {chart.title} [{chart.difficulty_name}]"
        click.echo(f"{'would unrank' if dry_run else 'unranking'} {chart.hash[:16]} {chart.note_count} notes {chart.duration_ms / 1000:.1f}s {name}")

    if dry_run:
        click.echo(f"{len(charts)} ranked charts have fewer than {minimum_notes} notes or are shorter than {minimum_ms / 1000:g} seconds")
        return

    for chart in charts:
        set_chart_status(chart, ChartStatus.UNRANKED, None)

    db.session.commit()

    for chart in charts:
        tasks.enqueue(tasks.apply_chart_status, chart.id)

    click.echo(f"{len(charts)} charts unranked")


@nascent_cli.command("review-play")
@click.argument("play_id", type=int)
@click.argument("decision", type=click.Choice(["approve", "reject"]))
@click.option("--note", default="", help="Why, kept with the play")
def review(play_id: int, decision: str, note: str):
    play = db.session.get(Play, play_id)

    if play is None:
        raise click.ClickException(f"no play with id {play_id}")

    review_play(play, None, decision == "approve", note)
    db.session.commit()
    ranking.sync_leaderboard(play.user)
    ranked = "ranked" if play.ranked else "not ranked"
    click.echo(f"play {play.id} by {play.user.username} on {play.chart.title} [{play.chart.difficulty_name}] is now {play.status} and {ranked}")


@nascent_cli.command("recalculate")
@click.option("--resimulate", is_flag=True, help="Re-judge every stored replay too, for judge changes")
def recalculate(resimulate: bool):
    chart_ids = list(db.session.scalars(select(Chart.id).order_by(Chart.id)).all())
    jobs = tasks.enqueue_recalculation(chart_ids, resimulate)
    final = tasks.enqueue(tasks.refresh_all_users, depends_on=jobs)
    _report(final, f"recalculated {len(chart_ids)} charts with calculator {calculator_version()}")

    if final:
        click.echo(f"queued {len(jobs)} jobs for {len(chart_ids)} charts")


@nascent_cli.command("apply-play-changes")
@click.option("--user", "username", default=None, help="Only this player's plays")
@click.option("--chart", "chart_hash", default=None, help="Only plays on the chart with this hash")
def apply_play_changes(username: str | None, chart_hash: str | None):
    players = select(Play.user_id).distinct()
    chart_id = None

    if username:
        players = players.where(Play.user_id == _get_user(username).id)

    if chart_hash:
        chart = get_chart(chart_hash)

        if chart is None:
            raise click.ClickException(f"no chart with hash {chart_hash}")

        chart_id = chart.id
        players = players.where(Play.chart_id == chart.id)

    user_ids = sorted(db.session.scalars(players).all())
    jobs = tasks.enqueue_play_changes(user_ids, chart_id)

    if jobs:
        click.echo(f"queued {len(jobs)} jobs for {len(user_ids)} players")
    else:
        click.echo(f"refreshed the bests and stats of {len(user_ids)} players")


@nascent_cli.command("calculator-status")
@click.option("--require-current", is_flag=True, help="Fail if anything still uses an older calculator version")
def calculator_status(require_current: bool):
    version = calculator_version()
    older = 0

    for label, model in (("charts", Chart), ("chart difficulties", ChartDifficulty), ("plays", Play)):
        total = db.session.scalar(select(func.count()).select_from(model)) or 0
        stale = db.session.scalar(select(func.count()).select_from(model).where(model.calculator_version != version)) or 0
        older += stale
        click.echo(f"{label}: {total - stale} on the current calculator, {stale} on an older one")

    pending = tasks.pending_jobs()

    if pending is not None:
        click.echo(
            f"background jobs: {pending['queued']} queued, {pending['running']} running, {pending['waiting']} waiting on others, "
            f"{pending['stuck']} stuck behind failed jobs, {pending['retrying']} retrying, {pending['failed']} failed"
        )

    click.echo(f"current calculator {version}")

    if require_current and older:
        raise click.ClickException(f"{older} rows still use an older calculator")


@nascent_cli.command("wait-for-jobs")
@click.option("--timeout", type=int, default=6 * 3600, help="Give up after this many seconds")
@click.option("--interval", type=int, default=15, help="Seconds between progress reports")
def wait_for_jobs(timeout: int, interval: int):
    pending = tasks.pending_jobs()

    if pending is None:
        click.echo("tasks run inline, so there is nothing to wait for")
        return

    failed_before = pending["failed"]
    deadline = time.monotonic() + timeout

    while True:
        outstanding = pending["queued"] + pending["running"] + pending["waiting"] + pending["retrying"]

        if pending["failed"] > failed_before:
            raise click.ClickException(f"{pending['failed'] - failed_before} jobs failed; read them with rq info or the worker logs")

        if outstanding == 0:
            stuck = f"; {pending['stuck']} jobs are stuck behind failed jobs and will never run" if pending["stuck"] else ""
            click.echo(f"every background job has finished{stuck}")
            return

        if time.monotonic() > deadline:
            raise click.ClickException(f"gave up after {timeout}s with {outstanding} jobs still outstanding")

        click.echo(f"{pending['queued']} queued, {pending['running']} running, {pending['waiting']} waiting on others, {pending['retrying']} retrying")
        time.sleep(interval)
        pending = tasks.pending_jobs()


def _quantiles(values: list[float]) -> dict[str, float]:
    if not values:
        return {"count": 0}

    ordered = sorted(values)

    def pick(fraction: float) -> float:
        return ordered[min(len(ordered) - 1, int(fraction * (len(ordered) - 1) + 0.5))]

    return {"count": len(ordered), "p10": pick(0.1), "p50": pick(0.5), "p90": pick(0.9), "max": ordered[-1]}


def _difficulty_report() -> dict:
    boards = {}

    for key_count in ranking.ranked_key_counts():
        difficulties = list(db.session.scalars(select(Chart.difficulty).where(Chart.key_count == key_count, Chart.status == ChartStatus.RANKED)))
        board = ranking.leaderboard_users_query(key_count).subquery()
        players = db.session.execute(
            select(board.c.user_id, User.username, board.c.performance)
            .join(User, User.id == board.c.user_id)
            .order_by(board.c.performance.desc(), board.c.user_id.asc())
        ).all()
        boards[str(key_count)] = {
            "chart_difficulty": _quantiles(difficulties),
            "players": [{"id": row.user_id, "name": row.username, "performance": row.performance} for row in players],
        }

    return {"calculator": calculator_version(), "generated_at": timeutil.utcnow().isoformat(), "boards": boards}


def _compare_reports(before: dict, after: dict, top: int) -> list[str]:
    lines = [f"calculator {before.get('calculator')} -> {after['calculator']}"]

    for key_count, board in after["boards"].items():
        previous = before.get("boards", {}).get(key_count, {"chart_difficulty": {}, "players": []})
        old_rank = {player["id"]: (index + 1, player["performance"]) for index, player in enumerate(previous["players"])}
        new_rank = {player["id"]: index + 1 for index, player in enumerate(board["players"])}
        old_spread, new_spread = previous["chart_difficulty"], board["chart_difficulty"]
        lines.append(
            f"{key_count}K: {new_spread.get('count', 0)} ranked charts, median difficulty {old_spread.get('p50', 0):.2f} -> {new_spread.get('p50', 0):.2f}, "
            f"90th percentile {old_spread.get('p90', 0):.2f} -> {new_spread.get('p90', 0):.2f}"
        )
        lines.append(f"  {len(board['players'])} players on the leaderboard (was {len(previous['players'])}); new top {top}:")

        for index, player in enumerate(board["players"][:top]):
            rank, performance = old_rank.get(player["id"], (None, None))
            moved = "new" if rank is None else f"{rank - (index + 1):+d}"
            was = "" if performance is None else f" (was #{rank}, {performance:.2f})"
            lines.append(f"  #{index + 1:<4} {player['name']:24} {player['performance']:10.2f}  {moved:>5}{was}")

        names = {player["id"]: player["name"] for player in board["players"]}
        movers = sorted((abs(new_rank[pid] - rank), pid, rank) for pid, (rank, _) in old_rank.items() if pid in new_rank and min(rank, new_rank[pid]) <= 200)
        lines.append("  biggest moves among the top 200:")

        for _, pid, rank in reversed(movers[-10:]):
            lines.append(f"    {names[pid]:24} #{rank} -> #{new_rank[pid]} ({rank - new_rank[pid]:+d})")

    return lines


@nascent_cli.command("difficulty-report")
@click.option("--compare-to", type=click.File("r"), default=None, help="An earlier report to compare against, or - for standard input")
@click.option("--top", type=int, default=25, help="Leaderboard places to list when comparing")
def difficulty_report(compare_to, top: int):
    report = _difficulty_report()

    if compare_to is None:
        click.echo(json.dumps(report))
        return

    for line in _compare_reports(json.load(compare_to), report, top):
        click.echo(line)


@nascent_cli.command("refresh-users")
def refresh_users():
    _report(tasks.enqueue(tasks.refresh_all_users), "refreshed every player's stats")


@nascent_cli.command("rebuild-leaderboard")
def rebuild_leaderboard():
    for board, size in ranking.rebuild_leaderboard().items():
        click.echo(f"{board}: {size} players")


@nascent_cli.command("snapshot-ranks")
def snapshot_ranks():
    click.echo(f"recorded today's rank for {ranking.snapshot_all()} leaderboard entries")


@nascent_cli.command("prune")
def prune():
    now = timeutil.utcnow()
    sessions = db.session.execute(delete(AuthSession).where((AuthSession.refresh_expires_at < now) | AuthSession.revoked_at.is_not(None))).rowcount
    tickets = db.session.execute(
        delete(PlayTicket).where(PlayTicket.used_at.is_(None), (PlayTicket.expires_at < now) | PlayTicket.superseded_at.is_not(None))
    ).rowcount
    resets = db.session.execute(delete(PasswordReset).where(PasswordReset.expires_at < now)).rowcount
    db.session.commit()
    click.echo(f"removed {sessions} sessions, {tickets} unused tickets and {resets} password reset links")


@nascent_cli.command("worker")
@click.option("--burst", is_flag=True, help="Exit once the queue is empty")
def worker(burst: bool):
    queue = tasks.queue()

    if queue is None:
        raise click.ClickException("the worker needs NASCENT_REDIS_URL and NASCENT_TASKS_INLINE=0")

    from rq import SimpleWorker, Worker

    tasks.set_worker_app(current_app._get_current_object())
    worker_class = Worker if hasattr(os, "fork") else SimpleWorker
    worker_class([queue], connection=queue.connection).work(burst=burst, with_scheduler=True)
