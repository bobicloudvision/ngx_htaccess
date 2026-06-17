# ngx_http_hello_module

[![CI](https://github.com/bobicloudvision/ngx_htaccess/actions/workflows/ci.yml/badge.svg)](https://github.com/bobicloudvision/ngx_htaccess/actions/workflows/ci.yml)

Minimal nginx HTTP module that responds with `Hello, World!` when the `hello` directive is used in a location block.

## Build (dynamic module)

```bash
cd /path/to/nginx/source
./configure --with-compat --add-dynamic-module=/path/to/ngx_htaccess
make modules
```

The compiled module is at `objs/ngx_http_hello_module.so`.

## Build (static module)

```bash
cd /path/to/nginx/source
./configure --add-module=/path/to/ngx_htaccess
make
sudo make install
```

## Usage

```nginx
load_module modules/ngx_http_hello_module.so;

http {
    server {
        listen 8080;

        location / {
            hello;
        }
    }
}
```

```bash
curl http://127.0.0.1:8080/
# Hello, World!
```

See [examples/nginx.conf](examples/nginx.conf).

## CI

GitHub Actions builds the dynamic module against nginx 1.30.2 and runs a smoke test on every push and pull request to `main`.
