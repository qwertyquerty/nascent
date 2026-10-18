import click
from flask import current_app
from flask.cli import with_appcontext

from .services import mail


@click.command("send-test-email")
@click.argument("address")
@with_appcontext
def send_test_email(address: str):
    if not mail.enabled():
        raise click.ClickException("mail is not set up, set nascent_smtp_host and the other nascent_smtp_* settings in the inventory")

    config = current_app.config

    try:
        mail.send(address, "Nascent test email", f"This is a test email from {config['SITE_URL']}. If it arrived, password reset emails work too.\n")
    except Exception as error:
        raise click.ClickException(f"sending through {config['SMTP_HOST']}:{config['SMTP_PORT']} failed: {error}") from error

    click.echo(f"sent a test email to {address} from {config['MAIL_FROM']} through {config['SMTP_HOST']}")
