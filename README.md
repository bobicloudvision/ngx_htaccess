# ngx_http_htaccess_module

[![CI](https://github.com/bobicloudvision/ngx_htaccess/actions/workflows/ci.yml/badge.svg)](https://github.com/bobicloudvision/ngx_htaccess/actions/workflows/ci.yml)

nginx HTTP module that reads Apache `.htaccess` files at request time with per-directory semantics and caches parsed rules for performance.

## Usage

```nginx
load_module modules/ngx_http_htaccess_module.so;

http {
    htaccess_cache_max_entries 1024;

    server {
        listen 8080;
        root /var/www/site;

        location / {
            htaccess on;
        }
    }
}
```

PHP applications (WordPress, etc.) still need a `fastcgi_pass` or proxy block for `.php` files. This module handles the Apache rewrite/auth/header layer from `.htaccess`.

## Directives

| Directive | Description |
|-----------|-------------|
| `htaccess on\|off` | Enable `.htaccess` processing for a location |
| `htaccess_file .htaccess` | Filename to read (default `.htaccess`) |
| `htaccess_cache on\|off` | Cache parsed rules per file (default `on`) |
| `htaccess_cache_max_entries N` | LRU cache size (default `1024`) |
| `htaccess_max_redirects N` | Internal rewrite loop guard (default `10`) |

## Supported `.htaccess` directives

- **mod_rewrite:** `RewriteEngine`, `RewriteBase`, `RewriteCond`, `RewriteRule` (flags: `L`, `END`, `R`, `NC`, `OR`, `C`, `QSA`, `E=`)
- **mod_alias:** `Redirect`, `RedirectMatch`, `RedirectPermanent`, `RedirectTemp`
- **mod_auth:** `AuthType`, `AuthName`, `AuthUserFile`, `Require`, `Order`, `Allow`, `Deny`
- **mod_headers:** `Header set/unset/append/merge`
- **mod_expires:** `ExpiresActive`, `ExpiresByType`, `ExpiresDefault`
- **core:** `ErrorDocument`, `Options -Indexes`, `DirectoryIndex`
- **blocks:** `<IfModule>`, `<Files>`, `<FilesMatch>`

Not supported: `php_value`, `php_flag`, Apache `<If>` / `<Else>` conditionals.

## Caching

Each `.htaccess` file is parsed once and stored in a per-worker LRU cache keyed by path. Entries are invalidated when `mtime` or `size` changes. Missing files are negative-cached to avoid repeated `stat()` calls.

## Build

```bash
cd /path/to/nginx/source
./configure --with-compat --add-dynamic-module=/path/to/ngx_htaccess
make modules
```

## Test

```bash
tests/integration/run.sh
tests/integration/wordpress.sh
tests/integration/opencart.sh
```

See [examples/nginx.conf](examples/nginx.conf) and [htaccess_examples/wordpress](htaccess_examples/wordpress).
