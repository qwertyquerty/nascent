"""password reset links

Revision ID: 0006
Revises: 0005

"""
from alembic import op
import sqlalchemy as sa

import nascent_server.models


revision = '0006'
down_revision = '0005'
branch_labels = None
depends_on = None


def upgrade():
    if sa.inspect(op.get_bind()).has_table('password_resets'):
        return

    op.create_table(
        'password_resets',
        sa.Column('id', sa.BigInteger().with_variant(sa.Integer(), 'sqlite'), nullable=False),
        sa.Column('user_id', sa.BigInteger().with_variant(sa.Integer(), 'sqlite'), nullable=False),
        sa.Column('token_hash', sa.String(length=64), nullable=False),
        sa.Column('password_check', sa.String(length=64), nullable=False),
        sa.Column('created_at', nascent_server.models.UTCDateTime(timezone=True), nullable=False),
        sa.Column('expires_at', nascent_server.models.UTCDateTime(timezone=True), nullable=False),
        sa.Column('used_at', nascent_server.models.UTCDateTime(timezone=True), nullable=True),
        sa.Column('ip', sa.String(length=45), nullable=False),
        sa.ForeignKeyConstraint(['user_id'], ['users.id'], name=op.f('fk_password_resets_user_id_users'), ondelete='CASCADE'),
        sa.PrimaryKeyConstraint('id', name=op.f('pk_password_resets')),
        sa.UniqueConstraint('token_hash', name=op.f('uq_password_resets_token_hash')),
    )

    with op.batch_alter_table('password_resets', schema=None) as batch_op:
        batch_op.create_index(batch_op.f('ix_password_resets_user_id'), ['user_id'], unique=False)


def downgrade():
    op.drop_table('password_resets')
