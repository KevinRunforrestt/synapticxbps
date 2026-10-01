#!/usr/bin/env bash
#
# build.sh — Build script for synaptic-xbps on Void Linux (musl, x86_64).
#
# This script automates the entire build of the XBPS-backed synaptic
# port on a Void Linux system. It is intended for use OUTSIDE the
# xbps-src chroot, as a quick "I just want the binary" path.
#
# Prerequisites (install on Void Linux before running):
#
#   xbps-install -S
#   xbps-install -u
#   xbps-install base-devel meson ninja pkg-config gettext glib-devel
#   xbps-install gtk+3-devel vte3-devel libxbps-devel libarchive-devel
#   xbps-install openssl-devel zlib-devel polkit-devel intltool itstool
#
# For the musl variant, additionally:
#   xbps-install base-chroot-musl  (if your system is glibc; otherwise
#                                   just run inside a musl chroot)
#
# Usage:
#   ./build.sh                # build in ./build, install to ./install
#   ./build.sh clean          # remove build/ and install/
#   ./build.sh package        # produce a .xbps via xbps-create
#   ./build.sh install        # install to / (requires root)
#
# Exit codes:
#   0   success
#   1   build configuration error
#   2   compilation failed
#   3   installation failed
#   4   packaging failed

set -euo pipefail

# Resolve script location
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="${SCRIPT_DIR}"
BUILD_DIR="${SCRIPT_DIR}/build"
INSTALL_DIR="${SCRIPT_DIR}/install"
LOG_FILE="${SCRIPT_DIR}/build.log"

# Detect target architecture (default: native)
ARCH="$(uname -m)"

# Read command
CMD="${1:-build}"

log()  { echo "[build.sh] $*"; tee -a "$LOG_FILE" >&2; }
fail() { log "ERROR: $*"; exit "${2:-1}"; }

clean_build() {
   log "Cleaning $BUILD_DIR and $INSTALL_DIR"
   rm -rf "$BUILD_DIR" "$INSTALL_DIR" "$LOG_FILE"
   [ "$CMD" = "clean" ] && exit 0
}

configure_build() {
   log "Configuring for $ARCH, backend=xbps"
   mkdir -p "$BUILD_DIR"

   # Meson setup. The -Dxbps=enabled forces HAVE_XBPS=1 and links against
   # libxbps instead of apt-pkg.
   cd "$BUILD_DIR"
   meson setup \
      --prefix=/usr \
      --libdir=lib \
      --buildtype=release \
      --default-library=shared \
      -Dxbps=enabled \
      -Dtests=false \
      -Dlaunchpad_integration=false \
      -Dpkg_hold=false \
      "$BUILD_DIR" \
      "$SOURCE_DIR" 2>&1 | tee -a "$LOG_FILE" \
      || fail "meson setup failed" 1
}

compile_build() {
   log "Compiling"
   cd "$BUILD_DIR"
   ninja 2>&1 | tee -a "$LOG_FILE" \
      || fail "ninja failed" 2
}

install_build() {
   log "Installing to $INSTALL_DIR"
   mkdir -p "$INSTALL_DIR"
   cd "$BUILD_DIR"
   DESTDIR="$INSTALL_DIR" ninja install 2>&1 | tee -a "$LOG_FILE" \
      || fail "ninja install failed" 3
}

package_build() {
   log "Packaging into .xbps"
   [ -d "$INSTALL_DIR" ] || fail "no install dir; run ./build.sh first" 1

   # Determine version from meson introspection
   VERSION="$(grep '^version' "${SOURCE_DIR}/meson.build" | head -1 | sed -E "s/.*'([^']+)'.*/\1/")"
   [ -z "$VERSION" ] && VERSION="0.91.7"

   PKGNAME="synaptic-xbps"
   PKGFILE="${SCRIPT_DIR}/${PKGNAME}-${VERSION}_1.${ARCH}.xbps"
   log "Creating $PKGFILE"

   # xbps-create makes a binary package out of a dest tree.
   cd "$INSTALL_DIR"
   xbps-create -A "$ARCH" \
      -n "${PKGNAME}-${VERSION}_1" \
      -s "GTK package manager for XBPS (Void Linux port)" \
      -l "GPL-2.0-or-later" \
      -m "Synaptic-XBPS port <port@example.com>" \
      --desc-greater "Graphical package manager built on top of libxbps,
the native Void Linux package system. This is a port of Synaptic
(originally a GTK frontend for APT) that uses libxbps instead." \
      --homepage "https://github.com/mvo5/synaptic" \
      "$INSTALL_DIR" 2>&1 | tee -a "$LOG_FILE" \
      || fail "xbps-create failed" 4
   log "Package: $(ls -la "${PKGNAME}"-*.xbps 2>/dev/null || true)"
}

case "$CMD" in
   clean)
      clean_build
      ;;
   configure)
      clean_build
      configure_build
      ;;
   build|"")
      [ -d "$BUILD_DIR" ] || configure_build
      compile_build
      install_build
      log "Done. Binary is at $INSTALL_DIR/usr/bin/synaptic"
      ;;
   package)
      [ -d "$BUILD_DIR" ] || { configure_build; }
      compile_build
      install_build
      package_build
      ;;
   install)
      [ -d "$BUILD_DIR" ] || configure_build
      compile_build
      log "Installing to / (sudo required)"
      sudo ninja -C "$BUILD_DIR" install
      ;;
   *)
      fail "Unknown command: $CMD (use clean|configure|build|package|install)"
      ;;
esac

exit 0
