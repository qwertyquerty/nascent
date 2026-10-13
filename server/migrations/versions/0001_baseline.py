"""baseline

Revision ID: 0001
Revises: 

"""
from alembic import op
import sqlalchemy as sa
import nascent_server.models


revision = '0001'
down_revision = None
branch_labels = None
depends_on = None


def upgrade():
    op.create_table('users',
    sa.Column('id', sa.BigInteger().with_variant(sa.Integer(), 'sqlite'), nullable=False),
    sa.Column('username', sa.String(length=32), nullable=False),
    sa.Column('username_key', sa.String(length=32), nullable=False),
    sa.Column('email', sa.String(length=254), nullable=False),
    sa.Column('email_key', sa.String(length=254), nullable=False),
    sa.Column('password_hash', sa.String(length=255), nullable=False),
    sa.Column('role', sa.String(length=16), nullable=False),
    sa.Column('is_banned', sa.Boolean(), server_default=sa.false(), nullable=False),
    sa.Column('created_at', nascent_server.models.UTCDateTime(timezone=True), nullable=False),
    sa.Column('play_count', sa.Integer(), nullable=False),
    sa.Column('ranked_play_count', sa.Integer(), nullable=False),
    sa.Column('total_hits', sa.BigInteger(), nullable=False),
    sa.Column('ranked_playtime_ms', sa.BigInteger(), nullable=False),
    sa.Column('total_playtime_ms', sa.BigInteger(), nullable=False),
    sa.Column('max_combo', sa.Integer(), nullable=False),
    sa.Column('last_played_at', nascent_server.models.UTCDateTime(timezone=True), nullable=True),
    sa.PrimaryKeyConstraint('id', name=op.f('pk_users')),
    sa.UniqueConstraint('email_key', name=op.f('uq_users_email_key')),
    sa.UniqueConstraint('username_key', name=op.f('uq_users_username_key'))
    )
    op.create_table('auth_sessions',
    sa.Column('id', sa.BigInteger().with_variant(sa.Integer(), 'sqlite'), nullable=False),
    sa.Column('user_id', sa.BigInteger().with_variant(sa.Integer(), 'sqlite'), nullable=False),
    sa.Column('access_hash', sa.String(length=64), nullable=False),
    sa.Column('access_expires_at', nascent_server.models.UTCDateTime(timezone=True), nullable=False),
    sa.Column('refresh_hash', sa.String(length=64), nullable=False),
    sa.Column('previous_refresh_hash', sa.String(length=64), nullable=True),
    sa.Column('refresh_expires_at', nascent_server.models.UTCDateTime(timezone=True), nullable=False),
    sa.Column('created_at', nascent_server.models.UTCDateTime(timezone=True), nullable=False),
    sa.Column('last_used_at', nascent_server.models.UTCDateTime(timezone=True), nullable=False),
    sa.Column('client', sa.String(length=200), nullable=False),
    sa.Column('ip', sa.String(length=45), nullable=False),
    sa.Column('revoked_at', nascent_server.models.UTCDateTime(timezone=True), nullable=True),
    sa.ForeignKeyConstraint(['user_id'], ['users.id'], name=op.f('fk_auth_sessions_user_id_users'), ondelete='CASCADE'),
    sa.PrimaryKeyConstraint('id', name=op.f('pk_auth_sessions')),
    sa.UniqueConstraint('access_hash', name=op.f('uq_auth_sessions_access_hash')),
    sa.UniqueConstraint('refresh_hash', name=op.f('uq_auth_sessions_refresh_hash'))
    )
    with op.batch_alter_table('auth_sessions', schema=None) as batch_op:
        batch_op.create_index(batch_op.f('ix_auth_sessions_previous_refresh_hash'), ['previous_refresh_hash'], unique=False)
        batch_op.create_index(batch_op.f('ix_auth_sessions_user_id'), ['user_id'], unique=False)

    op.create_table('charts',
    sa.Column('id', sa.BigInteger().with_variant(sa.Integer(), 'sqlite'), nullable=False),
    sa.Column('hash', sa.String(length=64), nullable=False),
    sa.Column('status', sa.String(length=16), nullable=False),
    sa.Column('key_count', sa.SmallInteger(), nullable=False),
    sa.Column('hit_accuracy', sa.SmallInteger(), nullable=False),
    sa.Column('damage', sa.SmallInteger(), nullable=False),
    sa.Column('title', sa.String(length=255), nullable=False),
    sa.Column('title_unicode', sa.String(length=255), nullable=False),
    sa.Column('artist', sa.String(length=255), nullable=False),
    sa.Column('artist_unicode', sa.String(length=255), nullable=False),
    sa.Column('mapper', sa.String(length=255), nullable=False),
    sa.Column('difficulty_name', sa.String(length=255), nullable=False),
    sa.Column('note_count', sa.Integer(), nullable=False),
    sa.Column('hold_count', sa.Integer(), nullable=False),
    sa.Column('duration_ms', sa.Integer(), nullable=False),
    sa.Column('difficulty', sa.Float(), nullable=False),
    sa.Column('calculator_version', sa.String(length=16), nullable=False),
    sa.Column('data_key', sa.String(length=128), nullable=False),
    sa.Column('play_count', sa.Integer(), nullable=False),
    sa.Column('uploaded_by_id', sa.BigInteger().with_variant(sa.Integer(), 'sqlite'), nullable=True),
    sa.Column('created_at', nascent_server.models.UTCDateTime(timezone=True), nullable=False),
    sa.Column('ranked_at', nascent_server.models.UTCDateTime(timezone=True), nullable=True),
    sa.Column('ranked_by_id', sa.BigInteger().with_variant(sa.Integer(), 'sqlite'), nullable=True),
    sa.ForeignKeyConstraint(['ranked_by_id'], ['users.id'], name=op.f('fk_charts_ranked_by_id_users'), ondelete='SET NULL'),
    sa.ForeignKeyConstraint(['uploaded_by_id'], ['users.id'], name=op.f('fk_charts_uploaded_by_id_users'), ondelete='SET NULL'),
    sa.PrimaryKeyConstraint('id', name=op.f('pk_charts')),
    sa.UniqueConstraint('hash', name=op.f('uq_charts_hash'))
    )
    with op.batch_alter_table('charts', schema=None) as batch_op:
        batch_op.create_index('ix_charts_created_at', ['created_at'], unique=False)
        batch_op.create_index('ix_charts_status_difficulty', ['status', 'difficulty'], unique=False)

    op.create_table('play_tickets',
    sa.Column('id', sa.BigInteger().with_variant(sa.Integer(), 'sqlite'), nullable=False),
    sa.Column('token_hash', sa.String(length=64), nullable=False),
    sa.Column('user_id', sa.BigInteger().with_variant(sa.Integer(), 'sqlite'), nullable=False),
    sa.Column('chart_hash', sa.String(length=64), nullable=False),
    sa.Column('rate_key', sa.SmallInteger(), nullable=False),
    sa.Column('issued_at', nascent_server.models.UTCDateTime(timezone=True), nullable=False),
    sa.Column('expires_at', nascent_server.models.UTCDateTime(timezone=True), nullable=False),
    sa.Column('used_at', nascent_server.models.UTCDateTime(timezone=True), nullable=True),
    sa.Column('superseded_at', nascent_server.models.UTCDateTime(timezone=True), nullable=True),
    sa.ForeignKeyConstraint(['user_id'], ['users.id'], name=op.f('fk_play_tickets_user_id_users'), ondelete='CASCADE'),
    sa.PrimaryKeyConstraint('id', name=op.f('pk_play_tickets')),
    sa.UniqueConstraint('token_hash', name=op.f('uq_play_tickets_token_hash'))
    )
    with op.batch_alter_table('play_tickets', schema=None) as batch_op:
        batch_op.create_index('ix_play_tickets_expires_at', ['expires_at'], unique=False)
        batch_op.create_index('ix_play_tickets_user_issued', ['user_id', 'issued_at'], unique=False)

    op.create_table('user_key_stats',
    sa.Column('user_id', sa.BigInteger().with_variant(sa.Integer(), 'sqlite'), nullable=False),
    sa.Column('key_count', sa.SmallInteger(), nullable=False),
    sa.Column('performance', sa.Float(), nullable=False),
    sa.Column('accuracy', sa.Float(), nullable=False),
    sa.Column('play_count', sa.Integer(), nullable=False),
    sa.Column('ranked_play_count', sa.Integer(), nullable=False),
    sa.Column('total_hits', sa.BigInteger(), nullable=False),
    sa.Column('ranked_playtime_ms', sa.BigInteger(), nullable=False),
    sa.Column('total_playtime_ms', sa.BigInteger(), nullable=False),
    sa.Column('max_combo', sa.Integer(), nullable=False),
    sa.ForeignKeyConstraint(['user_id'], ['users.id'], name=op.f('fk_user_key_stats_user_id_users'), ondelete='CASCADE'),
    sa.PrimaryKeyConstraint('user_id', 'key_count', name=op.f('pk_user_key_stats'))
    )
    with op.batch_alter_table('user_key_stats', schema=None) as batch_op:
        batch_op.create_index('ix_user_key_stats_performance', ['key_count', 'performance', 'user_id'], unique=False)

    op.create_table('chart_difficulties',
    sa.Column('chart_id', sa.BigInteger().with_variant(sa.Integer(), 'sqlite'), nullable=False),
    sa.Column('rate_key', sa.SmallInteger(), nullable=False),
    sa.Column('difficulty', sa.Float(), nullable=False),
    sa.Column('calculator_version', sa.String(length=16), nullable=False),
    sa.ForeignKeyConstraint(['chart_id'], ['charts.id'], name=op.f('fk_chart_difficulties_chart_id_charts'), ondelete='CASCADE'),
    sa.PrimaryKeyConstraint('chart_id', 'rate_key', name=op.f('pk_chart_difficulties'))
    )
    op.create_table('chart_sources',
    sa.Column('id', sa.BigInteger().with_variant(sa.Integer(), 'sqlite'), nullable=False),
    sa.Column('chart_id', sa.BigInteger().with_variant(sa.Integer(), 'sqlite'), nullable=False),
    sa.Column('source', sa.String(length=16), nullable=False),
    sa.Column('checksum', sa.String(length=64), nullable=False),
    sa.Column('source_id', sa.BigInteger(), nullable=True),
    sa.Column('source_set_id', sa.BigInteger(), nullable=True),
    sa.Column('created_at', nascent_server.models.UTCDateTime(timezone=True), nullable=False),
    sa.ForeignKeyConstraint(['chart_id'], ['charts.id'], name=op.f('fk_chart_sources_chart_id_charts'), ondelete='CASCADE'),
    sa.PrimaryKeyConstraint('id', name=op.f('pk_chart_sources')),
    sa.UniqueConstraint('source', 'checksum', name='uq_chart_sources_source_checksum')
    )
    with op.batch_alter_table('chart_sources', schema=None) as batch_op:
        batch_op.create_index(batch_op.f('ix_chart_sources_chart_id'), ['chart_id'], unique=False)

    op.create_table('plays',
    sa.Column('id', sa.BigInteger().with_variant(sa.Integer(), 'sqlite'), nullable=False),
    sa.Column('user_id', sa.BigInteger().with_variant(sa.Integer(), 'sqlite'), nullable=False),
    sa.Column('chart_id', sa.BigInteger().with_variant(sa.Integer(), 'sqlite'), nullable=False),
    sa.Column('rate_key', sa.SmallInteger(), nullable=False),
    sa.Column('key_count', sa.SmallInteger(), nullable=False),
    sa.Column('status', sa.String(length=16), nullable=False),
    sa.Column('eligible', sa.Boolean(), nullable=False),
    sa.Column('ranked', sa.Boolean(), nullable=False),
    sa.Column('accuracy', sa.Float(), nullable=False),
    sa.Column('grade', sa.String(length=2), nullable=False),
    sa.Column('max_combo', sa.Integer(), nullable=False),
    sa.Column('count_perfect', sa.Integer(), nullable=False),
    sa.Column('count_amazing', sa.Integer(), nullable=False),
    sa.Column('count_great', sa.Integer(), nullable=False),
    sa.Column('count_good', sa.Integer(), nullable=False),
    sa.Column('count_bad', sa.Integer(), nullable=False),
    sa.Column('count_miss', sa.Integer(), nullable=False),
    sa.Column('average_error', sa.Float(), nullable=False),
    sa.Column('hit_count', sa.Integer(), nullable=False),
    sa.Column('duration_ms', sa.Integer(), nullable=False),
    sa.Column('difficulty', sa.Float(), nullable=False),
    sa.Column('performance', sa.Float(), nullable=False),
    sa.Column('calculator_version', sa.String(length=16), nullable=False),
    sa.Column('is_chart_best', sa.Boolean(), nullable=False),
    sa.Column('is_profile_best', sa.Boolean(), nullable=False),
    sa.Column('replay_hash', sa.String(length=64), nullable=False),
    sa.Column('replay_key', sa.String(length=128), nullable=False),
    sa.Column('settings', sa.JSON(), nullable=False),
    sa.Column('flags', sa.JSON(), nullable=False),
    sa.Column('submitted_at', nascent_server.models.UTCDateTime(timezone=True), nullable=False),
    sa.Column('ticket_id', sa.BigInteger().with_variant(sa.Integer(), 'sqlite'), nullable=True),
    sa.Column('reviewed_by_id', sa.BigInteger().with_variant(sa.Integer(), 'sqlite'), nullable=True),
    sa.Column('reviewed_at', nascent_server.models.UTCDateTime(timezone=True), nullable=True),
    sa.Column('review_note', sa.String(length=500), nullable=False),
    sa.ForeignKeyConstraint(['chart_id'], ['charts.id'], name=op.f('fk_plays_chart_id_charts'), ondelete='CASCADE'),
    sa.ForeignKeyConstraint(['reviewed_by_id'], ['users.id'], name=op.f('fk_plays_reviewed_by_id_users'), ondelete='SET NULL'),
    sa.ForeignKeyConstraint(['ticket_id'], ['play_tickets.id'], name=op.f('fk_plays_ticket_id_play_tickets'), ondelete='SET NULL'),
    sa.ForeignKeyConstraint(['user_id'], ['users.id'], name=op.f('fk_plays_user_id_users'), ondelete='CASCADE'),
    sa.PrimaryKeyConstraint('id', name=op.f('pk_plays')),
    sa.UniqueConstraint('replay_hash', name=op.f('uq_plays_replay_hash'))
    )
    with op.batch_alter_table('plays', schema=None) as batch_op:
        batch_op.create_index('ix_plays_chart_leaderboard', ['chart_id', 'rate_key', sa.literal_column('accuracy DESC'), sa.literal_column('max_combo DESC'), 'id'], unique=False, postgresql_where=sa.text('is_chart_best'), sqlite_where=sa.text('is_chart_best'))
        batch_op.create_index('ix_plays_chart_user', ['chart_id', 'user_id'], unique=False)
        batch_op.create_index('ix_plays_profile_best', ['user_id', sa.literal_column('performance DESC')], unique=False, postgresql_where=sa.text('is_profile_best'), sqlite_where=sa.text('is_profile_best'))
        batch_op.create_index('ix_plays_profile_best_keys', ['user_id', 'key_count', sa.literal_column('performance DESC')], unique=False, postgresql_where=sa.text('is_profile_best'), sqlite_where=sa.text('is_profile_best'))
        batch_op.create_index('ix_plays_status', ['status', 'id'], unique=False)
        batch_op.create_index('ix_plays_user_chart', ['user_id', 'chart_id', 'rate_key'], unique=False)
        batch_op.create_index('ix_plays_user_recent', ['user_id', 'id'], unique=False)



def downgrade():
    with op.batch_alter_table('plays', schema=None) as batch_op:
        batch_op.drop_index('ix_plays_user_recent')
        batch_op.drop_index('ix_plays_user_chart')
        batch_op.drop_index('ix_plays_status')
        batch_op.drop_index('ix_plays_profile_best_keys', postgresql_where=sa.text('is_profile_best'), sqlite_where=sa.text('is_profile_best'))
        batch_op.drop_index('ix_plays_profile_best', postgresql_where=sa.text('is_profile_best'), sqlite_where=sa.text('is_profile_best'))
        batch_op.drop_index('ix_plays_chart_user')
        batch_op.drop_index('ix_plays_chart_leaderboard', postgresql_where=sa.text('is_chart_best'), sqlite_where=sa.text('is_chart_best'))

    op.drop_table('plays')
    with op.batch_alter_table('chart_sources', schema=None) as batch_op:
        batch_op.drop_index(batch_op.f('ix_chart_sources_chart_id'))

    op.drop_table('chart_sources')
    op.drop_table('chart_difficulties')
    with op.batch_alter_table('user_key_stats', schema=None) as batch_op:
        batch_op.drop_index('ix_user_key_stats_performance')

    op.drop_table('user_key_stats')
    with op.batch_alter_table('play_tickets', schema=None) as batch_op:
        batch_op.drop_index('ix_play_tickets_user_issued')
        batch_op.drop_index('ix_play_tickets_expires_at')

    op.drop_table('play_tickets')
    with op.batch_alter_table('charts', schema=None) as batch_op:
        batch_op.drop_index('ix_charts_status_difficulty')
        batch_op.drop_index('ix_charts_created_at')

    op.drop_table('charts')
    with op.batch_alter_table('auth_sessions', schema=None) as batch_op:
        batch_op.drop_index(batch_op.f('ix_auth_sessions_user_id'))
        batch_op.drop_index(batch_op.f('ix_auth_sessions_previous_refresh_hash'))

    op.drop_table('auth_sessions')
    op.drop_table('users')
