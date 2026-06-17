#!/bin/sh
set -eu

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
FIXTURE="$ROOT/htaccess_examples/wordpress"
DOCROOT="/tmp/ngx-htaccess-wp-$$"
NGINX_VERSION="${NGINX_VERSION:-1.30.2}"

cleanup() {
    if [ -n "${NGINX_PID:-}" ]; then
        kill "$NGINX_PID" 2>/dev/null || true
        wait "$NGINX_PID" 2>/dev/null || true
    fi
    rm -rf "$DOCROOT"
}
trap cleanup EXIT

mkdir -p "$DOCROOT"
cp "$FIXTURE" "$DOCROOT/.htaccess"
echo 'wordpress' > "$DOCROOT/index.php"

if [ ! -d "/tmp/nginx-${NGINX_VERSION}" ]; then
    wget -q "https://nginx.org/download/nginx-${NGINX_VERSION}.tar.gz" -O "/tmp/nginx-${NGINX_VERSION}.tar.gz"
    tar xzf "/tmp/nginx-${NGINX_VERSION}.tar.gz" -C /tmp
    cd "/tmp/nginx-${NGINX_VERSION}"
    ./configure --with-compat --add-dynamic-module="$ROOT" >/dev/null
    make -j"$(nproc)" >/dev/null
fi

cd "/tmp/nginx-${NGINX_VERSION}"
MODULE="$(pwd)/objs/ngx_http_htaccess_module.so"
NGINX="$(pwd)/objs/nginx"

cat > /tmp/ngx-htaccess-wp.conf <<EOF
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
        listen 18082;
        root ${DOCROOT};
        access_log off;
        location / { htaccess on; }
    }
}
EOF

"$NGINX" -t -c /tmp/ngx-htaccess-wp.conf
"$NGINX" -c /tmp/ngx-htaccess-wp.conf &
NGINX_PID=$!

for _ in $(seq 1 30); do
    body="$(curl -fsS http://127.0.0.1:18082/missing-page 2>/dev/null || true)"
    if echo "$body" | grep -q 'wordpress'; then
        echo "wordpress rewrite test passed"
        exit 0
    fi
    sleep 0.5
done

echo "wordpress rewrite test failed"
cat "${DOCROOT}/error.log" 2>/dev/null || true
exit 1
