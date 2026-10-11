import json
import os
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
from .services import ranking
from .services.calculator import calculator_version
from .services.charts import add_chart_source, get_chart, parse_chart, read_osu_file, register_chart, set_chart_status

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
