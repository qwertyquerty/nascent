"""reports

Revision ID: 0008
Revises: 0007

"""
from alembic import op
import sqlalchemy as sa

import nascent_server.models


revision = '0008'
down_revision = '0007'
branch_labels = None
depends_on = None


def upgrade():
    if sa.inspect(op.get_bind()).has_table('reports'):
        return

    big_id = sa.BigInteger().with_variant(sa.Integer(), 'sqlite')
    op.create_table(
        'reports',
        sa.Column('id', big_id, nullable=False),
        sa.Column('kind', sa.String(length=16), nullable=False),
        sa.Column('reason', sa.String(length=32), nullable=False),
        sa.Column('details', sa.String(length=1000), nullable=False),
        sa.Column('status', sa.String(length=16), nullable=False),
        sa.Column('reporter_id', big_id, nullable=True),
        sa.Column('user_id', big_id, nullable=True),
        sa.Column('play_id', big_id, nullable=True),
        sa.Column('chart_id', big_id, nullable=True),
        sa.Column('created_at', nascent_server.models.UTCDateTime(timezone=True), nullable=False),
        sa.Column('resolved_by_id', big_id, nullable=True),
        sa.Column('resolved_at', nascent_server.models.UTCDateTime(timezone=True), nullable=True),
        sa.Column('resolution_note', sa.String(length=500), nullable=False),
        sa.ForeignKeyConstraint(['reporter_id'], ['users.id'], name=op.f('fk_reports_reporter_id_users'), ondelete='SET NULL'),
        sa.ForeignKeyConstraint(['user_id'], ['users.id'], name=op.f('fk_reports_user_id_users'), ondelete='CASCADE'),
        sa.ForeignKeyConstraint(['play_id'], ['plays.id'], name=op.f('fk_reports_play_id_plays'), ondelete='CASCADE'),
        sa.ForeignKeyConstraint(['chart_id'], ['charts.id'], name=op.f('fk_reports_chart_id_charts'), ondelete='CASCADE'),
        sa.ForeignKeyConstraint(['resolved_by_id'], ['users.id'], name=op.f('fk_reports_resolved_by_id_users'), ondelete='SET NULL'),
        sa.PrimaryKeyConstraint('id', name=op.f('pk_reports')),
    )

    with op.batch_alter_table('reports', schema=None) as batch_op:
        batch_op.create_index(batch_op.f('ix_reports_reporter_id'), ['reporter_id'], unique=False)
        batch_op.create_index(batch_op.f('ix_reports_user_id'), ['user_id'], unique=False)
        batch_op.create_index(batch_op.f('ix_reports_play_id'), ['play_id'], unique=False)
        batch_op.create_index(batch_op.f('ix_reports_chart_id'), ['chart_id'], unique=False)
        batch_op.create_index('ix_reports_status', ['status', 'id'], unique=False)


def downgrade():
    op.drop_table('reports')
