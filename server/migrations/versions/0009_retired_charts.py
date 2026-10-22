"""retired charts

Revision ID: 0009
Revises: 0008

"""
from alembic import op
import sqlalchemy as sa

import nascent_server.models


revision = '0009'
down_revision = '0008'
branch_labels = None
depends_on = None


def upgrade():
    if sa.inspect(op.get_bind()).has_table('retired_charts'):
        return

    big_id = sa.BigInteger().with_variant(sa.Integer(), 'sqlite')
    op.create_table(
        'retired_charts',
        sa.Column('hash', sa.String(length=64), nullable=False),
        sa.Column('replaced_by_id', big_id, nullable=True),
        sa.Column('retired_at', nascent_server.models.UTCDateTime(timezone=True), nullable=False),
        sa.ForeignKeyConstraint(['replaced_by_id'], ['charts.id'], name=op.f('fk_retired_charts_replaced_by_id_charts'), ondelete='SET NULL'),
        sa.PrimaryKeyConstraint('hash', name=op.f('pk_retired_charts')),
    )

    with op.batch_alter_table('retired_charts', schema=None) as batch_op:
        batch_op.create_index(batch_op.f('ix_retired_charts_replaced_by_id'), ['replaced_by_id'], unique=False)


def downgrade():
    op.drop_table('retired_charts')
