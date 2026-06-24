#!/usr/bin/env bash
#
# Build the ngx_http_htaccess dynamic module against an nginx.org release and
# package it as a .deb or .rpm with nfpm. Designed to run inside a per-distro
# container (see .github/workflows/release.yml) but works on any Linux host.
#
# Usage:
#   packaging/build.sh deb        # produce a .deb
#   packaging/build.sh rpm        # produce a .rpm
#
# Env (all optional):
#   NGINX_VERSION   nginx branch to build against        (default 1.30.2)
#   PKG_VERSION     module package version                (default 0.1.0)
#   PKG_ARCH        amd64 | arm64                         (default: host arch)
#   PKG_RELEASE     distro tag for the release field      (default: autodetect)
#   OUT_DIR         where packages are written            (default ./dist)
#   NFPM_VERSION    nfpm release to fetch if not present  (default 2.41.1)
set -euo pipefail

PACKAGER="${1:?usage: build.sh <deb|rpm>}"
case "$PACKAGER" in deb|rpm) ;; *) echo "packager must be deb or rpm" >&2; exit 2 ;; esac

ADDON_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NGINX_VERSION="${NGINX_VERSION:-1.30.2}"
PKG_VERSION="${PKG_VERSION:-0.1.0}"
OUT_DIR="${OUT_DIR:-$ADDON_DIR/dist}"
NFPM_VERSION="${NFPM_VERSION:-2.41.1}"

# ---------------------------------------------------------------------------
# Detect host arch -> nfpm arch
# ---------------------------------------------------------------------------
if [ -z "${PKG_ARCH:-}" ]; then
  case "$(uname -m)" in
    x86_64|amd64)  PKG_ARCH=amd64 ;;
    aarch64|arm64) PKG_ARCH=arm64 ;;
    *) echo "unsupported arch $(uname -m)" >&2; exit 2 ;;
  esac
fi

# ---------------------------------------------------------------------------
# Install build dependencies + a release tag, detecting the package manager.
# ---------------------------------------------------------------------------
detect_release_tag() {
  . /etc/os-release 2>/dev/null || true
  case "${ID:-}" in
    debian) echo "deb${VERSION_ID%%.*}" ;;
    ubuntu) echo "ubuntu${VERSION_ID//./}" ;;
    rhel|rocky|almalinux|centos) echo "el${VERSION_ID%%.*}" ;;
    fedora) echo "fc${VERSION_ID}" ;;
    opensuse*|sles|sled) echo "suse${VERSION_ID%%.*}" ;;
    *) echo "linux" ;;
  esac
}
PKG_RELEASE="${PKG_RELEASE:-$(detect_release_tag)}"

# Add a package to the list only if the named command is missing — avoids the
# curl vs curl-minimal conflict on RHEL-family images.
maybe() { command -v "$1" >/dev/null 2>&1 || echo "$2"; }

install_build_deps() {
  if command -v apt-get >/dev/null 2>&1; then
    export DEBIAN_FRONTEND=noninteractive
    apt-get update
    apt-get install -y --no-install-recommends \
      build-essential libpcre2-dev zlib1g-dev libssl-dev libcrypt-dev ca-certificates gettext-base \
      $(maybe curl curl) $(maybe tar tar)
  elif command -v dnf >/dev/null 2>&1; then
    dnf install -y gcc make pcre2-devel zlib-devel openssl-devel libxcrypt-devel gettext \
      $(maybe curl curl) $(maybe tar tar) $(maybe find findutils)
  elif command -v yum >/dev/null 2>&1; then
    yum install -y gcc make pcre2-devel zlib-devel openssl-devel libxcrypt-devel gettext \
      $(maybe curl curl) $(maybe tar tar) $(maybe find findutils)
  elif command -v zypper >/dev/null 2>&1; then
    zypper --non-interactive install -y gcc make pcre2-devel zlib-devel libopenssl-devel libxcrypt-devel gettext-runtime \
      $(maybe curl curl) $(maybe tar tar)
  else
    echo "no supported package manager found" >&2; exit 2
  fi
}

# ---------------------------------------------------------------------------
# Fetch nfpm static binary if it isn't on PATH.
# ---------------------------------------------------------------------------
ensure_nfpm() {
  if command -v nfpm >/dev/null 2>&1; then return; fi
  local arch tarball
  case "$PKG_ARCH" in amd64) arch=x86_64 ;; arm64) arch=arm64 ;; esac
  tarball="nfpm_${NFPM_VERSION}_Linux_${arch}.tar.gz"
  curl -fsSL "https://github.com/goreleaser/nfpm/releases/download/v${NFPM_VERSION}/${tarball}" \
    -o /tmp/nfpm.tar.gz
  tar -C /usr/local/bin -xzf /tmp/nfpm.tar.gz nfpm
}

# ---------------------------------------------------------------------------
# Build the dynamic module against the matching nginx source.
# ---------------------------------------------------------------------------
build_module() {
  local work
  work="$(mktemp -d)"
  curl -fsSL "https://nginx.org/download/nginx-${NGINX_VERSION}.tar.gz" -o "$work/nginx.tar.gz"
  tar -C "$work" -xzf "$work/nginx.tar.gz"
  (
    cd "$work/nginx-${NGINX_VERSION}"
    ./configure --with-compat --add-dynamic-module="$ADDON_DIR"
    make -j"$(nproc 2>/dev/null || echo 2)" modules
  )
  MODULE_SO="$work/nginx-${NGINX_VERSION}/objs/ngx_http_htaccess_module.so"
  test -f "$MODULE_SO"
}

install_build_deps
ensure_nfpm
build_module

mkdir -p "$OUT_DIR"
export PKG_VERSION PKG_ARCH PKG_RELEASE NGINX_VERSION MODULE_SO ADDON_DIR
echo "Packaging $PACKAGER: nginx-module-htaccess ${PKG_VERSION}-${PKG_RELEASE} (${PKG_ARCH}) against nginx ${NGINX_VERSION}"

# nfpm does not expand env vars in the config, so render the template first.
rendered="$(mktemp --suffix=.yaml)"
envsubst < "$ADDON_DIR/packaging/nfpm.yaml" > "$rendered"
nfpm package --config "$rendered" --packager "$PACKAGER" --target "$OUT_DIR"

echo "Built:"
ls -1 "$OUT_DIR"
