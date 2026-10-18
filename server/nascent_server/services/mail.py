import smtplib
import ssl
from email.message import EmailMessage

from flask import current_app

OUTBOX = "nascent_mail_outbox"


def enabled() -> bool:
    return current_app.config["MAIL_BACKEND"] != "none"


def outbox() -> list[EmailMessage]:
    return current_app.extensions.setdefault(OUTBOX, [])


def _send_smtp(message: EmailMessage) -> None:
    config = current_app.config
    security = config["SMTP_SECURITY"]
    context = ssl.create_default_context()

    if security == "ssl":
        client = smtplib.SMTP_SSL(config["SMTP_HOST"], config["SMTP_PORT"], timeout=config["SMTP_TIMEOUT"], context=context)
    else:
        client = smtplib.SMTP(config["SMTP_HOST"], config["SMTP_PORT"], timeout=config["SMTP_TIMEOUT"])

    with client:
        if security == "starttls":
            client.starttls(context=context)

        if config["SMTP_USERNAME"]:
            client.login(config["SMTP_USERNAME"], config["SMTP_PASSWORD"])

        client.send_message(message)


def send(to: str, subject: str, body: str) -> None:
    backend = current_app.config["MAIL_BACKEND"]
    message = EmailMessage()
    message["From"] = current_app.config["MAIL_FROM"]
    message["To"] = to
    message["Subject"] = subject
    message.set_content(body)

    if backend == "smtp":
        _send_smtp(message)
    elif backend == "memory":
        outbox().append(message)
    elif backend == "log":
        current_app.logger.warning("mail to %s: %s\n%s", to, subject, body)
    else:
        raise RuntimeError("mail is not set up, set NASCENT_SMTP_HOST")
