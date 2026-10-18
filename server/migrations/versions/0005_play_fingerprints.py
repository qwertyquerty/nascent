"""store a playing-style fingerprint with each play

Revision ID: 0005
Revises: 0004

"""
from alembic import op
import sqlalchemy as sa


revision = '0005'
down_revision = '0004'
branch_labels = None
depends_on = None


def upgrade():
    if 'fingerprint' in {column['name'] for column in sa.inspect(op.get_bind()).get_columns('plays')}:
        return

    with op.batch_alter_table('plays', schema=None) as batch_op:
        batch_op.add_column(sa.Column('fingerprint', sa.JSON(), nullable=True))


def downgrade():
    with op.batch_alter_table('plays', schema=None) as batch_op:
        batch_op.drop_column('fingerprint')
