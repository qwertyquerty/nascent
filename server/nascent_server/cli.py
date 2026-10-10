import json
import os
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

import click
from flask import current_app
from flask.cli import AppGroup
from sqlalchemy import delete, select

from . import tasks, timeutil
from .errors import ApiError
from .extensions import db
from .models import AuthSession, Chart, ChartStatus, PlayTicket, Role, User
from .security import hash_password
from .services import osu, ranking
from .services.calculator import calculator_version
from .services.charts import get_chart, parse_chart, register_chart, set_chart_status

nascent_cli = AppGroup("nascent", help="Nascent server administration")

OSU_IMPORT_STATE = "osu-ranked-import.json"
OSU_DOWNLOAD_WORKERS = 8
OSU_PROGRESS_EVERY = 500


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


@nascent_cli.command("set-role")
@click.argument("username")
@click.argument("role", type=click.Choice([role.value for role in Role]))
def set_role(username: str, role: str):
    user = db.session.scalar(select(User).where(User.username_key == username.lower()))

    if user is None:
        raise click.ClickException(f"no user named {username}")

    user.role = role
    db.session.commit()
    click.echo(f"{username} is now {role}")


def _import_chart(data: dict, label: str, rank: bool, verbose: bool = True) -> str | None:
    try:
        native = parse_chart(data)
        chart, created = register_chart(native, None)
        changed = set_chart_status(chart, ChartStatus.RANKED, None) if rank else False
    except (ApiError, ValueError) as error:
        db.session.rollback()
        click.echo(f"skipped {label}: {getattr(error, 'message', error)}")
        return None

    db.session.commit()

    if created:
        tasks.enqueue(tasks.fill_chart_difficulties, chart.id)

    if changed:
        tasks.enqueue(tasks.apply_chart_status, chart.id)

    if verbose:
        name = f"{chart.artist} - {chart.title} [{chart.difficulty_name}]"
        click.echo(f"{'added' if created else 'exists'} {chart.hash[:16]} {chart.key_count:2}K {chart.status:8} {chart.difficulty:7.2f} {name}")

    return "added" if created else "exists"


@nascent_cli.command("import-charts")
@click.argument("path", type=click.Path(exists=True, path_type=Path))
@click.option("--rank", is_flag=True, help="Rank every imported chart")
def import_charts(path: Path, rank: bool):
    files = [path] if path.is_file() else sorted(path.rglob("*.json"))
    imported = 0

    for file in files:
        try:
            data = json.loads(file.read_text(encoding="utf-8"))
        except ValueError as error:
            click.echo(f"skipped {file}: {error}")
            continue

        imported += _import_chart(data, str(file), rank) == "added"

    click.echo(f"{imported} new charts")


@nascent_cli.command("import-osu")
@click.argument("url")
@click.option("--mirror", default=osu.DEFAULT_MIRROR, show_default=True, help="Where beatmapsets are downloaded from, with {id} for the set id")
def import_osu(url: str, mirror: str):
    try:
        files = osu.beatmap_files(url, mirror)
    except osu.OsuError as error:
        raise click.ClickException(str(error)) from None

    imported = 0

    for name, text in files:
        try:
            data = osu.convert(text)
        except (KeyError, IndexError, ValueError) as error:
            click.echo(f"skipped {name}: unreadable beatmap ({error})")
            continue

        if data is None:
            click.echo(f"skipped {name}: not an osu!mania beatmap")
            continue

        imported += _import_chart(data, name, True) == "added"

    click.echo(f"{imported} new ranked charts")


def _save_import_state(path: Path, state: dict[str, str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(state, separators=(",", ":")), encoding="utf-8")
    temporary.replace(path)


@nascent_cli.command("import-ranked-osu")
@click.option("--search", default=osu.DEFAULT_SEARCH, show_default=True, help="Ranked osu!mania beatmapset search, with {limit} and {offset}")
@click.option("--source", default=osu.DEFAULT_FILE_SOURCE, show_default=True, help="Where .osu files are downloaded from, with {id} for the beatmap id")
@click.option("--per-minute", default=osu.DEFAULT_REQUESTS_PER_MINUTE, show_default=True, help="Most downloads to start per minute")
def import_ranked_osu(search: str, source: str, per_minute: int):
    state_path = Path(current_app.config["STORAGE_PATH"]) / OSU_IMPORT_STATE
    state = json.loads(state_path.read_text(encoding="utf-8")) if state_path.exists() else {}
    key_counts = set(current_app.config["RANKED_KEY_COUNTS"])

    try:
        beatmaps = osu.ranked_mania_beatmaps(search)
    except osu.OsuError as error:
        raise click.ClickException(str(error)) from None

    rankable = [beatmap for beatmap in beatmaps if beatmap.key_count in key_counts]
    pending = [beatmap for beatmap in rankable if state.get(str(beatmap.beatmap_id)) != beatmap.checksum]
    click.echo(f"{len(beatmaps)} ranked osu!mania difficulties, {len(rankable)} with a rankable key count, {len(pending)} not imported yet")

    limiter = osu.RateLimiter(per_minute)
    results = Counter()

    with ThreadPoolExecutor(OSU_DOWNLOAD_WORKERS) as pool:
        futures = {pool.submit(osu.download_beatmap, beatmap, source, limiter): beatmap for beatmap in pending}

        for done, future in enumerate(as_completed(futures), 1):
            beatmap = futures[future]

            try:
                data = osu.convert(future.result())
            except (osu.OsuError, KeyError, IndexError, ValueError) as error:
                click.echo(f"skipped {beatmap.label}: {error}")
                results["failed"] += 1
            else:
                outcome = _import_chart(data, beatmap.label, True, verbose=False) if data is not None else None
                results[outcome or "failed"] += 1

                if outcome is not None:
                    state[str(beatmap.beatmap_id)] = beatmap.checksum

            if done % OSU_PROGRESS_EVERY == 0 or done == len(pending):
                _save_import_state(state_path, state)
                click.echo(f"{done}/{len(pending)}: {results['added']} added, {results['exists']} already there, {results['failed']} failed")

    click.echo(f"{results['added']} new ranked charts, {results['exists']} already on the server, {results['failed']} failed and will be retried next run")


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
