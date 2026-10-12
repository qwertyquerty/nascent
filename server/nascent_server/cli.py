import json
import os
import sys
from collections import Counter
from datetime import UTC, datetime
from pathlib import Path

import click
from flask import current_app
from flask.cli import AppGroup
from sqlalchemy import delete, func, select

from . import tasks, timeutil
from .errors import ApiError
from .extensions import db
from .models import AuthSession, Chart, ChartStatus, PlayTicket, Role, User
from .security import hash_password, revoke_user_sessions
from .services import ranking
from .services.calculator import calculator_version
from .services.charts import add_chart_source, get_chart, parse_chart, read_osu_file, register_chart, set_chart_status
from .services.plays import submit_play

nascent_cli = AppGroup("nascent", help="Nascent server administration")

CHART_SUFFIXES = (".osu", ".json")


def _report(job: str | None, done: str) -> None:
    click.echo(f"queued job {job}" if job else done)


@nascent_cli.command("create-db")
def create_db():
    db.create_all()
    click.echo("created any missing tables")


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


def _read_chart_file(file: Path) -> tuple[dict, dict | None]:
    if file.suffix.lower() == ".osu":
        return read_osu_file(file.read_bytes())

    return json.loads(file.read_text(encoding="utf-8")), None


@nascent_cli.command("import-charts")
@click.argument("path", type=click.Path(exists=True, path_type=Path))
@click.option("--rank", is_flag=True, help="Rank every imported chart")
def import_charts(path: Path, rank: bool):
    files = [path] if path.is_file() else sorted(file for file in path.rglob("*") if file.suffix.lower() in CHART_SUFFIXES)
    imported = 0

    if not files:
        raise click.ClickException(f"no .osu or .json chart files in {path} that this user can read")

    for file in files:
        try:
            data, source = _read_chart_file(file)
            native = parse_chart(data)
            chart, created = register_chart(native, None)

            if source is not None:
                add_chart_source(chart, **source)

            changed = set_chart_status(chart, ChartStatus.RANKED, None) if rank else False
        except (ApiError, ValueError) as error:
            db.session.rollback()
            click.echo(f"skipped {file}: {getattr(error, 'message', error)}")
            continue

        db.session.commit()

        if created:
            tasks.enqueue(tasks.fill_chart_difficulties, chart.id)

        if changed:
            tasks.enqueue(tasks.apply_chart_status, chart.id)

        imported += created
        name = f"{chart.artist} - {chart.title} [{chart.difficulty_name}]"
        click.echo(f"{'added' if created else 'exists'} {chart.hash[:16]} {chart.key_count:2}K {chart.status:8} {chart.difficulty:7.2f} {name}")

    click.echo(f"{imported} new charts")


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


@nascent_cli.command("recalculate")
@click.option("--resimulate", is_flag=True, help="Re-judge every stored replay too, for judge changes")
def recalculate(resimulate: bool):
    chart_ids = db.session.scalars(select(Chart.id).order_by(Chart.id)).all()
    jobs = [job for job in (tasks.enqueue(tasks.recalculate_chart, chart_id, resimulate) for chart_id in chart_ids) if job]
    final = tasks.enqueue(tasks.refresh_all_users, depends_on=jobs)
    _report(final, f"recalculated {len(chart_ids)} charts with calculator {calculator_version()}")

    if final:
        click.echo(f"queued {len(jobs)} chart jobs")


@nascent_cli.command("refresh-users")
def refresh_users():
    _report(tasks.enqueue(tasks.refresh_all_users), "refreshed every player's stats")


@nascent_cli.command("rebuild-leaderboard")
def rebuild_leaderboard():
    for board, size in ranking.rebuild_leaderboard().items():
        click.echo(f"{board}: {size} players")


@nascent_cli.command("prune")
def prune():
    now = timeutil.utcnow()
    sessions = db.session.execute(delete(AuthSession).where((AuthSession.refresh_expires_at < now) | AuthSession.revoked_at.is_not(None))).rowcount
    tickets = db.session.execute(
        delete(PlayTicket).where(PlayTicket.used_at.is_(None), (PlayTicket.expires_at < now) | PlayTicket.superseded_at.is_not(None))
    ).rowcount
    db.session.commit()
    click.echo(f"removed {sessions} sessions and {tickets} unused tickets")


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
