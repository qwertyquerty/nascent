#!/bin/sh
set -e

certificate=/etc/letsencrypt/live/nascent/fullchain.pem
template=/etc/nginx/nascent/http.conf.template

NASCENT_DOMAINS="${NASCENT_DOMAINS:-$NASCENT_DOMAIN}"
export NASCENT_DOMAINS

if [ -n "$NASCENT_DOMAIN" ] && [ -f "$certificate" ]; then
    template=/etc/nginx/nascent/https.conf.template
    echo "serving https for $NASCENT_DOMAINS"
else
    echo "no certificate yet, serving http only"
fi

envsubst '${NASCENT_DOMAIN} ${NASCENT_DOMAINS}' < "$template" > /etc/nginx/conf.d/default.conf
echo "set \$nascent_api ${NASCENT_API:-api:8000};" > /etc/nginx/nascent-api.conf

if [ "${1:-}" = "reload" ]; then
    nginx -t -q
    nginx -s reload
    echo "reloaded, sending the api to ${NASCENT_API:-api:8000}"
    exit 0
fi

(
    while true; do
        sleep 21600
        nginx -s reload
    done
) &

exec /docker-entrypoint.sh nginx -g 'daemon off;'
