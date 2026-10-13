"""reconcile databases created before migrations

Revision ID: 0002
Revises: 0001

"""
from alembic import op
import sqlalchemy as sa


revision = '0002'
down_revision = '0001'
branch_labels = None
depends_on = None


def upgrade():
    inspector = sa.inspect(op.get_bind())
    user_columns = {column['name'] for column in inspector.get_columns('users')}
    user_indexes = {index['name'] for index in inspector.get_indexes('users')}
    play_indexes = {index['name'] for index in inspector.get_indexes('plays')}

    with op.batch_alter_table('users', schema=None) as batch_op:
        if 'ix_users_performance' in user_indexes:
            batch_op.drop_index('ix_users_performance')

        for column in ('performance', 'accuracy'):
            if column in user_columns:
                batch_op.drop_column(column)

    with op.batch_alter_table('plays', schema=None) as batch_op:
        if 'ix_plays_chart_performance' not in play_indexes:
            batch_op.create_index('ix_plays_chart_performance', ['chart_id', sa.literal_column('performance DESC'), sa.literal_column('accuracy DESC'), 'id'], unique=False, postgresql_where=sa.text('is_profile_best'), sqlite_where=sa.text('is_profile_best'))

        if 'ix_plays_ranked_performance' not in play_indexes:
            batch_op.create_index('ix_plays_ranked_performance', [sa.literal_column('performance DESC'), 'id'], unique=False, postgresql_where=sa.text('ranked'), sqlite_where=sa.text('ranked'))


def downgrade():
    with op.batch_alter_table('plays', schema=None) as batch_op:
        batch_op.drop_index('ix_plays_ranked_performance', postgresql_where=sa.text('ranked'), sqlite_where=sa.text('ranked'))
        batch_op.drop_index('ix_plays_chart_performance', postgresql_where=sa.text('is_profile_best'), sqlite_where=sa.text('is_profile_best'))
