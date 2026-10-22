"""chart modified flag

Revision ID: 0010
Revises: 0009

"""
from alembic import op
import sqlalchemy as sa


revision = '0010'
down_revision = '0009'
branch_labels = None
depends_on = None


def upgrade():
    if 'modified' in {column['name'] for column in sa.inspect(op.get_bind()).get_columns('charts')}:
        return

    with op.batch_alter_table('charts', schema=None) as batch_op:
        batch_op.add_column(sa.Column('modified', sa.Boolean(), server_default=sa.false(), nullable=False))


def downgrade():
    with op.batch_alter_table('charts', schema=None) as batch_op:
        batch_op.drop_column('modified')
