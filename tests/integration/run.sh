#!/bin/sh
set -eu

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
FIXTURE="$ROOT/tests/fixtures/redirect"
DOCROOT="/tmp/ngx-htaccess-test-$$"
NGINX_VERSION="${NGINX_VERSION:-1.30.2}"

cleanup() {
    if [ -n "${NGINX_PID:-}" ]; then
        kill "$NGINX_PID" 2>/dev/null || true
        wait "$NGINX_PID" 2>/dev/null || true
    fi
    rm -rf "$DOCROOT"
    rm -rf "/tmp/nginx-${NGINX_VERSION}"
    rm -f "/tmp/nginx-${NGINX_VERSION}.tar.gz"
}
trap cleanup EXIT

mkdir -p "$DOCROOT"
cp "$FIXTURE/.htaccess" "$DOCROOT/"

if [ ! -f "/tmp/nginx-${NGINX_VERSION}.tar.gz" ]; then
    wget -q "https://nginx.org/download/nginx-${NGINX_VERSION}.tar.gz" -O "/tmp/nginx-${NGINX_VERSION}.tar.gz"
fi

tar xzf "/tmp/nginx-${NGINX_VERSION}.tar.gz" -C /tmp
cd "/tmp/nginx-${NGINX_VERSION}"

./configure --with-compat --add-dynamic-module="$ROOT" >/dev/null
make -j"$(nproc)" >/dev/null

MODULE="$(pwd)/objs/ngx_http_htaccess_module.so"
NGINX="$(pwd)/objs/nginx"

cat > /tmp/ngx-htaccess-test.conf <<EOF
load_module ${MODULE};
daemon off;
pid ${DOCROOT}/nginx.pid;
error_log ${DOCROOT}/error.log;
events { worker_connections 32; }
http {
    client_body_temp_path ${DOCROOT}/client_body;
    proxy_temp_path ${DOCROOT}/proxy;
    fastcgi_temp_path ${DOCROOT}/fastcgi;
    uwsgi_temp_path ${DOCROOT}/uwsgi;
    scgi_temp_path ${DOCROOT}/scgi;
    server {
        listen 18081;
        root ${DOCROOT};
        access_log off;
        location / { htaccess on; }
    }
}
EOF

"$NGINX" -t -c /tmp/ngx-htaccess-test.conf
"$NGINX" -c /tmp/ngx-htaccess-test.conf &
NGINX_PID=$!

for _ in $(seq 1 30); do
    if curl -fsSI http://127.0.0.1:18081/old 2>/dev/null | grep -q '301'; then
        echo "redirect test passed"
        exit 0
    fi
    sleep 0.5
done

echo "redirect test failed"
cat "${DOCROOT}/error.log" 2>/dev/null || true
exit 1
