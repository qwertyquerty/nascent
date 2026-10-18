import zlib

import click
import nascent_core as nc
from flask.cli import with_appcontext
from sqlalchemy import select
from sqlalchemy.orm import joinedload

from .extensions import db
from .models import Play
from .services import anticheat
from .services.charts import native_chart
from .storage import get_json_gz


@click.command("backfill-fingerprints")
@click.option("--batch", default=200, show_default=True, help="Plays to process before each commit")
@with_appcontext
def backfill_fingerprints(batch: int):
    natives = {}
    last_id = 0
    filled = skipped = 0

    while True:
        plays = db.session.scalars(
            select(Play).options(joinedload(Play.chart)).where(Play.fingerprint.is_(None), Play.id > last_id).order_by(Play.id).limit(batch)
        ).all()

        if not plays:
            break

        for play in plays:
            last_id = play.id

            try:
                if play.chart_id not in natives:
                    natives[play.chart_id] = native_chart(play.chart)

                replay = nc.Replay.from_json(get_json_gz(play.replay_key))
                play.fingerprint = anticheat.fingerprint(nc.simulate(natives[play.chart_id], replay), replay.inputs)
            except (OSError, EOFError, ValueError, RuntimeError, zlib.error) as error:
                skipped += 1
                click.echo(f"skipped play {play.id}: {error}")
                continue

            filled += play.fingerprint is not None
            skipped += play.fingerprint is None

        db.session.commit()

    click.echo(f"fingerprinted {filled} plays, {skipped} had too few notes or could not be read")
