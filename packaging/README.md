# Packaging

Builds `.deb` and `.rpm` packages of the `ngx_http_htaccess` dynamic module.

## How it works

`ngx_http_htaccess_module.so` is a **dynamic nginx module**, so it is ABI-tied
to the nginx it loads into. These packages target **nginx.org's** official
repository, whose binaries are built `--with-compat`. The module is therefore
compiled `--with-compat` against the matching nginx *minor branch* (default
`1.30.2`) and is portable across distros for that branch. We still build the
`.so` inside each distro's container so its glibc / `libcrypt` linkage and the
declared package dependencies are correct for that distro.

| Format | Package name             | Installs to                                        |
|--------|--------------------------|----------------------------------------------------|
| deb    | `nginx-module-htaccess`  | `/usr/lib/nginx/modules/ngx_http_htaccess_module.so` |
| rpm    | `nginx-module-htaccess`  | same                                               |

A disabled load snippet is also installed to
`/usr/share/nginx/modules-available/mod-http-htaccess.conf`.

## Build locally

Run inside a container of the target distro (so deps/linkage match):

```bash
# .deb on Debian/Ubuntu
docker run --rm -v "$PWD":/src -w /src debian:12 packaging/build.sh deb

# .rpm on RHEL family
docker run --rm -v "$PWD":/src -w /src rockylinux:9 packaging/build.sh rpm
```

Output lands in `dist/`. Override defaults with env vars:

```bash
NGINX_VERSION=1.28.0 PKG_VERSION=1.0.0 packaging/build.sh deb
```

## Build in CI

`.github/workflows/release.yml` runs the full matrix (Debian 11/12,
Ubuntu 20.04/22.04/24.04, Rocky 8/9, Fedora 40/41, openSUSE Leap 15.6) on a
`v*` tag push, then attaches every package to the GitHub release. The nginx
branch is `1.30.2` by default, overridable via the workflow_dispatch input.

## Install & enable

```bash
sudo apt install ./nginx-module-htaccess_<ver>_amd64.deb     # deb
sudo dnf install ./nginx-module-htaccess-<ver>.x86_64.rpm    # rpm
```

Then add to the **main** context of `/etc/nginx/nginx.conf`:

```nginx
load_module modules/ngx_http_htaccess_module.so;
```

and `htaccess on;` in the relevant location. Restart nginx.

> The package depends on `nginx` from nginx.org. If the installed nginx is from
> a different minor branch than the one the module was built against, nginx will
> refuse to load the module — install the matching package build.
