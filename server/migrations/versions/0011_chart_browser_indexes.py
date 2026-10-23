"""chart browser and play history indexes

Revision ID: 0011
Revises: 0010

"""
import logging

from alembic import op
import sqlalchemy as sa
from sqlalchemy.exc import DBAPIError


revision = '0011'
down_revision = '0010'
branch_labels = None
depends_on = None

logger = logging.getLogger('alembic.runtime.migration')

SORT_COLUMNS = ('title', 'artist', 'mapper', 'difficulty', 'duration_ms', 'play_count', 'key_count')
SEARCH_COLUMNS = ('title', 'title_unicode', 'artist', 'artist_unicode', 'mapper', 'difficulty_name')


def _indexes(table):
    return {index['name'] for index in sa.inspect(op.get_bind()).get_indexes(table)}


def _create_search_index():
    bind = op.get_bind()

    if bind.dialect.name != 'postgresql' or 'ix_charts_search' in _indexes('charts'):
        return

    try:
        with bind.begin_nested():
            bind.execute(sa.text('CREATE EXTENSION IF NOT EXISTS pg_trgm'))
    except DBAPIError:
        logger.warning('could not enable pg_trgm, chart search stays unindexed')
        return

    columns = ', '.join(f'{column} gin_trgm_ops' for column in SEARCH_COLUMNS)
    bind.execute(sa.text(f'CREATE INDEX ix_charts_search ON charts USING gin ({columns})'))


def upgrade():
    if 'downloadable' not in {column['name'] for column in sa.inspect(op.get_bind()).get_columns('charts')}:
        with op.batch_alter_table('charts', schema=None) as batch_op:
            batch_op.add_column(sa.Column('downloadable', sa.Boolean(), server_default=sa.false(), nullable=False))

        op.execute(
            "UPDATE charts SET downloadable = EXISTS (SELECT 1 FROM chart_sources WHERE chart_sources.chart_id = charts.id"
            " AND chart_sources.source IN ('osu', 'etterna') AND chart_sources.source_set_id IS NOT NULL)"
        )

    existing = _indexes('charts')

    with op.batch_alter_table('charts', schema=None) as batch_op:
        for column in SORT_COLUMNS:
            if f'ix_charts_{column}_id' not in existing:
                batch_op.create_index(f'ix_charts_{column}_id', [column, 'id'], unique=False)

    if 'ix_plays_user_submitted_at' not in _indexes('plays'):
        with op.batch_alter_table('plays', schema=None) as batch_op:
            batch_op.create_index('ix_plays_user_submitted_at', ['user_id', 'submitted_at'], unique=False)

    _create_search_index()


def downgrade():
    if op.get_bind().dialect.name == 'postgresql':
        op.execute('DROP INDEX IF EXISTS ix_charts_search')

    with op.batch_alter_table('plays', schema=None) as batch_op:
        batch_op.drop_index('ix_plays_user_submitted_at')

    with op.batch_alter_table('charts', schema=None) as batch_op:
        for column in SORT_COLUMNS:
            batch_op.drop_index(f'ix_charts_{column}_id')

        batch_op.drop_column('downloadable')
