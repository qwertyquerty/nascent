FROM python:3.13-slim-trixie AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends g++ \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY server/pyproject.toml server/pyproject.toml
RUN python -c "import tomllib; project = tomllib.load(open('server/pyproject.toml', 'rb'))['project']; print('\n'.join(project['dependencies'] + project['optional-dependencies']['production']))" >requirements.txt \
    && pip wheel --no-cache-dir --wheel-dir /wheels -r requirements.txt

COPY src/core src/core
COPY src/libs/json src/libs/json
COPY src/libs/picosha2 src/libs/picosha2
COPY server/native server/native
RUN pip wheel --no-cache-dir --no-deps --wheel-dir /wheels ./server/native

FROM python:3.13-slim-trixie

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    NASCENT_STORAGE_PATH=/storage

RUN useradd --system --uid 10001 --create-home nascent \
    && mkdir /storage \
    && chown nascent /storage

WORKDIR /app
COPY --from=build /wheels /wheels
RUN pip install --no-cache-dir --no-index /wheels/*.whl \
    && rm -rf /wheels

COPY server/pyproject.toml ./
COPY server/nascent_server nascent_server
COPY server/migrations migrations
COPY server/wsgi.py server/deploy/uwsgi.ini ./
RUN pip install --no-cache-dir --no-deps -e . \
    && pip check

USER nascent
EXPOSE 8000
CMD ["sh", "-c", "flask --app wsgi nascent create-db && exec uwsgi --ini uwsgi.ini"]
