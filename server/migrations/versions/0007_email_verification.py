"""email verification

Revision ID: 0007
Revises: 0006

"""
from alembic import op
import sqlalchemy as sa

import nascent_server.models


revision = '0007'
down_revision = '0006'
branch_labels = None
depends_on = None


def upgrade():
    inspector = sa.inspect(op.get_bind())

    if 'email_verified_at' not in {column['name'] for column in inspector.get_columns('users')}:
        with op.batch_alter_table('users', schema=None) as batch_op:
            batch_op.add_column(sa.Column('email_verified_at', nascent_server.models.UTCDateTime(timezone=True), nullable=True))

    if inspector.has_table('email_verifications'):
        return

    op.create_table(
        'email_verifications',
        sa.Column('id', sa.BigInteger().with_variant(sa.Integer(), 'sqlite'), nullable=False),
        sa.Column('user_id', sa.BigInteger().with_variant(sa.Integer(), 'sqlite'), nullable=False),
        sa.Column('token_hash', sa.String(length=64), nullable=False),
        sa.Column('email_key', sa.String(length=254), nullable=False),
        sa.Column('created_at', nascent_server.models.UTCDateTime(timezone=True), nullable=False),
        sa.Column('expires_at', nascent_server.models.UTCDateTime(timezone=True), nullable=False),
        sa.Column('used_at', nascent_server.models.UTCDateTime(timezone=True), nullable=True),
        sa.ForeignKeyConstraint(['user_id'], ['users.id'], name=op.f('fk_email_verifications_user_id_users'), ondelete='CASCADE'),
        sa.PrimaryKeyConstraint('id', name=op.f('pk_email_verifications')),
        sa.UniqueConstraint('token_hash', name=op.f('uq_email_verifications_token_hash')),
    )

    with op.batch_alter_table('email_verifications', schema=None) as batch_op:
        batch_op.create_index(batch_op.f('ix_email_verifications_user_id'), ['user_id'], unique=False)


def downgrade():
    op.drop_table('email_verifications')

    with op.batch_alter_table('users', schema=None) as batch_op:
        batch_op.drop_column('email_verified_at')
