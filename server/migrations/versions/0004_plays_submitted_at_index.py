"""index plays by submission time

Revision ID: 0004
Revises: 0003

"""
from alembic import op
import sqlalchemy as sa


revision = '0004'
down_revision = '0003'
branch_labels = None
depends_on = None


def upgrade():
    if 'ix_plays_submitted_at' in {index['name'] for index in sa.inspect(op.get_bind()).get_indexes('plays')}:
        return

    with op.batch_alter_table('plays', schema=None) as batch_op:
        batch_op.create_index('ix_plays_submitted_at', ['submitted_at'], unique=False)


def downgrade():
    with op.batch_alter_table('plays', schema=None) as batch_op:
        batch_op.drop_index('ix_plays_submitted_at')
