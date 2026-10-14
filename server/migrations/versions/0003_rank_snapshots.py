"""daily rank and performance snapshots

Revision ID: 0003
Revises: 0002

"""
from alembic import op
import sqlalchemy as sa


revision = '0003'
down_revision = '0002'
branch_labels = None
depends_on = None


def upgrade():
    if sa.inspect(op.get_bind()).has_table('rank_snapshots'):
        return

    op.create_table(
        'rank_snapshots',
        sa.Column('user_id', sa.BigInteger().with_variant(sa.Integer(), 'sqlite'), nullable=False),
        sa.Column('key_count', sa.SmallInteger(), nullable=False),
        sa.Column('day', sa.Date(), nullable=False),
        sa.Column('performance', sa.Float(), nullable=False),
        sa.Column('rank', sa.Integer(), nullable=True),
        sa.ForeignKeyConstraint(['user_id'], ['users.id'], name=op.f('fk_rank_snapshots_user_id_users'), ondelete='CASCADE'),
        sa.PrimaryKeyConstraint('user_id', 'key_count', 'day', name=op.f('pk_rank_snapshots')),
    )


def downgrade():
    op.drop_table('rank_snapshots')
