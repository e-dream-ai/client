#!/usr/bin/env bash
# build_appimage.sh — Build infinidream and package it as a self-contained AppImage.
#
# Usage:
#   cd client/client_generic/LinuxBuild
#   chmod +x build_appimage.sh
#   ./build_appimage.sh
#
# Output: infinidream-<arch>.AppImage in this directory.
#
# Requirements:
#   - Build tools: cmake, gcc, g++, make, perl
#   - curl        (downloads sources + linuxdeploy / appimagetool on first run)
#   - zig         (used ONLY to compile OpenSSL and FFmpeg against glibc 2.35;
#                  the main binary uses system g++ for compatibility with
#                  the pre-built libsioclient_tls.a static archive)
#                  Install: pacman -S zig  OR  apt install zig
#   - nasm/yasm   (FFmpeg assembly optimisations; skipped gracefully if absent)
#   - FUSE or kernel >= 5.13 with /dev/fuse available (for running AppImages).
#     The script sets APPIMAGE_EXTRACT_AND_RUN=1 automatically without FUSE.

set -euo pipefail

ARCH="$(uname -m)"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ---------------------------------------------------------------------------
# glibc compatibility target.
#
# OpenSSL and FFmpeg are compiled as shared libraries with zig cc targeting
# this glibc version, so the bundled .so files don't reference any symbol
# newer than GLIBC_2.35.
#
# The main infinidream binary is compiled with system g++ (needed for ABI
# compatibility with the pre-built libsioclient_tls.a archive).  A separate
# glibc_compat.c shim intercepts the ~11 symbols that g++ would otherwise
# bind to newer glibc versions (GLIBC_2.38 / GLIBC_2.43) and redirects them
# to GLIBC_2.2.5 via .symver assembly directives.
#
# Net result: every file in the AppImage requires glibc <= 2.35, which covers:
#   Ubuntu 22.04 LTS (supported until April 2027)
#   Debian 12 Bookworm
#   Fedora 36+
#   Linux Mint 21+
# ---------------------------------------------------------------------------
GLIBC_TARGET="${ARCH}-linux-gnu.2.35"

# ---------------------------------------------------------------------------
# Clean AppDir and the appimage cmake build tree unconditionally at startup.
# ---------------------------------------------------------------------------
rm -rf "${SCRIPT_DIR}/AppDir"
rm -rf "${SCRIPT_DIR}/build-appimage"

BUILD_DIR="${SCRIPT_DIR}/build-appimage"
APPDIR="${SCRIPT_DIR}/AppDir"
TOOLS_DIR="${SCRIPT_DIR}/appimage-tools"
DEPS_SRC="${SCRIPT_DIR}/deps-src"
RUNTIME_DIR="${SCRIPT_DIR}/../Runtime"

# ---------------------------------------------------------------------------
# Dependency versions (pinned for reproducibility)
# ---------------------------------------------------------------------------
OPENSSL_VERSION="3.5.0"
OPENSSL_TARBALL="openssl-${OPENSSL_VERSION}.tar.gz"
OPENSSL_URL="https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/${OPENSSL_TARBALL}"
OPENSSL_SRC_DIR="${DEPS_SRC}/openssl-${OPENSSL_VERSION}"
OPENSSL_INSTALL="${SCRIPT_DIR}/openssl-minimal"

FFMPEG_VERSION="7.1.1"
FFMPEG_TARBALL="ffmpeg-${FFMPEG_VERSION}.tar.xz"
FFMPEG_URL="https://ffmpeg.org/releases/${FFMPEG_TARBALL}"
FFMPEG_SRC_DIR="${DEPS_SRC}/ffmpeg-${FFMPEG_VERSION}"
FFMPEG_INSTALL="${SCRIPT_DIR}/ffmpeg-minimal"

# Bump this tag whenever the FFmpeg configure flags change (e.g. added --disable-vdpau).
# The tag is stored alongside the glibc target in the toolchain marker so a flag
# change forces a clean rebuild even if GLIBC_TARGET is unchanged.
FFMPEG_CONFIG_TAG="novdpau"

LINUXDEPLOY_BIN="linuxdeploy-${ARCH}.AppImage"
APPIMAGETOOL_BIN="appimagetool-${ARCH}.AppImage"

LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/${LINUXDEPLOY_BIN}"
APPIMAGETOOL_URL="https://github.com/AppImage/appimagetool/releases/download/continuous/${APPIMAGETOOL_BIN}"

OUTPUT_APPIMAGE="${SCRIPT_DIR}/infinidream-${ARCH}.AppImage"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
log() { echo "[build_appimage] $*"; }

require_cmd() {
    command -v "$1" &>/dev/null || { echo "ERROR: '$1' not found. Please install it."; exit 1; }
}

download_tool() {
    local url="$1" dest="$2"
    if [[ ! -x "${dest}" ]]; then
        log "Downloading $(basename "${dest}") ..."
        curl -fsSL --retry 3 -o "${dest}" "${url}"
        chmod +x "${dest}"
    else
        log "$(basename "${dest}") already present, skipping download."
    fi
}

download_source() {
    local url="$1" dest="$2"
    if [[ ! -f "${dest}" ]]; then
        log "Downloading $(basename "${dest}") ..."
        curl -fsSL --retry 3 -o "${dest}" "${url}"
    else
        log "$(basename "${dest}") already downloaded, skipping."
    fi
}

cache_valid() {
    local marker="$1" toolchain_marker="$2"
    [[ -f "${marker}" ]] &&
    [[ -f "${toolchain_marker}" ]] &&
    [[ "$(cat "${toolchain_marker}")" == "${GLIBC_TARGET}" ]]
}

# ---------------------------------------------------------------------------
# Preflight checks
# ---------------------------------------------------------------------------
require_cmd cmake
require_cmd gcc
require_cmd g++
require_cmd curl
require_cmd make
require_cmd perl
require_cmd zig

if [[ ! -e /dev/fuse ]]; then
    log "WARNING: /dev/fuse not found — enabling APPIMAGE_EXTRACT_AND_RUN=1 for tooling."
    export APPIMAGE_EXTRACT_AND_RUN=1
fi

mkdir -p "${TOOLS_DIR}" "${DEPS_SRC}"

# ---------------------------------------------------------------------------
# zig cc / zig c++ wrapper scripts (used ONLY for building OpenSSL and FFmpeg)
#
# zig cc wraps clang and enforces a hard glibc ABI ceiling at compile + link
# time via --target. The bundled OpenSSL and FFmpeg .so files must be glibc
# 2.35-clean because they are shipped inside the AppImage.
#
# NOTE: zig is NOT used for the main infinidream binary. zig c++ uses libc++
# (LLVM) rather than libstdc++, which is ABI-incompatible with the pre-built
# libsioclient_tls.a (compiled with g++/libstdc++). The main binary is built
# with system g++ + a glibc_compat.c shim that pins symbol versions.
#
# -isystem /usr/include  — expose system headers (zig bundles its own libc
#                          headers and doesn't search /usr/include by default)
# "$@" before -L/usr/lib — caller-supplied -L flags take priority over the
#                          system fallback so our custom OpenSSL/FFmpeg install
#                          dirs are preferred
# ---------------------------------------------------------------------------
ZIG_CC="${TOOLS_DIR}/zig-cc"
ZIG_CXX="${TOOLS_DIR}/zig-cxx"

cat > "${ZIG_CC}" <<EOF
#!/bin/sh
exec zig cc -target ${GLIBC_TARGET} -isystem /usr/include "\$@" -L/usr/lib
EOF
chmod +x "${ZIG_CC}"

cat > "${ZIG_CXX}" <<EOF
#!/bin/sh
exec zig c++ -target ${GLIBC_TARGET} -isystem /usr/include "\$@" -L/usr/lib
EOF
chmod +x "${ZIG_CXX}"

log "Dependency compiler wrappers target glibc ${GLIBC_TARGET##*gnu.}"

# ---------------------------------------------------------------------------
# Build OpenSSL from source (cached — skipped if already built for this target)
#
# Arch's system OpenSSL 3.6+ references __isoc23_strtol@GLIBC_2.38. Bundling
# it would break on Ubuntu 22.04. We build OpenSSL 3.4.x (LTS, supported
# until 2030) from source with the zig wrappers so the .so is glibc-2.35 clean.
# ---------------------------------------------------------------------------
OPENSSL_MARKER="${OPENSSL_INSTALL}/lib/pkgconfig/openssl.pc"
OPENSSL_TOOLCHAIN_MARKER="${OPENSSL_INSTALL}/.toolchain-target"

if cache_valid "${OPENSSL_MARKER}" "${OPENSSL_TOOLCHAIN_MARKER}"; then
    log "OpenSSL ${OPENSSL_VERSION} already built for ${GLIBC_TARGET}, skipping."
else
    [[ -f "${OPENSSL_MARKER}" ]] && {
        log "OpenSSL cache exists but for a different target — rebuilding ..."
        rm -rf "${OPENSSL_INSTALL}"
    }

    log "Building OpenSSL ${OPENSSL_VERSION} from source ..."
    download_source "${OPENSSL_URL}" "${DEPS_SRC}/${OPENSSL_TARBALL}"

    if [[ ! -d "${OPENSSL_SRC_DIR}" ]]; then
        log "Extracting OpenSSL source ..."
        tar -xf "${DEPS_SRC}/${OPENSSL_TARBALL}" -C "${DEPS_SRC}"
    fi

    pushd "${OPENSSL_SRC_DIR}" > /dev/null

    log "Configuring OpenSSL ${OPENSSL_VERSION} ..."
    ./Configure \
        --prefix="${OPENSSL_INSTALL}" \
        --openssldir="${OPENSSL_INSTALL}/etc/ssl" \
        --libdir=lib \
        linux-x86_64 \
        shared \
        no-zlib \
        no-tests \
        CC="${ZIG_CC}" \
        CXX="${ZIG_CXX}"

    log "Compiling OpenSSL libraries ($(nproc) jobs) ..."
    make -j"$(nproc)" build_libs   # libs only — skip the CLI app

    log "Installing OpenSSL to ${OPENSSL_INSTALL} ..."
    # Install to a tmpfs staging dir, then copy to the (possibly NTFS) destination.
    # cp -r never calls chmod, so it works on filesystems that reject permission
    # changes (NTFS mounts).  Symlinks in the staging tree become regular file
    # copies — fine for .so chains and headers.
    _ossl_tmp="$(mktemp -d)"
    make install_dev DESTDIR="${_ossl_tmp}"
    mkdir -p "${OPENSSL_INSTALL}"
    cp -r "${_ossl_tmp}${OPENSSL_INSTALL}/." "${OPENSSL_INSTALL}/"
    rm -rf "${_ossl_tmp}"

    echo "${GLIBC_TARGET}" > "${OPENSSL_TOOLCHAIN_MARKER}"
    popd > /dev/null
    log "OpenSSL build complete."
fi

# ---------------------------------------------------------------------------
# Build minimal FFmpeg from source (cached)
#
# --disable-everything + selective enables: h264/hevc decode, mov/mkv demux,
# file/http/https/tcp/tls protocols, TLS via our source-built OpenSSL.
# ---------------------------------------------------------------------------
FFMPEG_MARKER="${FFMPEG_INSTALL}/lib/pkgconfig/libavcodec.pc"
FFMPEG_TOOLCHAIN_MARKER="${FFMPEG_INSTALL}/.toolchain-target"
FFMPEG_EXPECTED_TAG="${GLIBC_TARGET}-${FFMPEG_CONFIG_TAG}"

if [[ -f "${FFMPEG_MARKER}" ]] && [[ -f "${FFMPEG_TOOLCHAIN_MARKER}" ]] && \
   [[ "$(cat "${FFMPEG_TOOLCHAIN_MARKER}")" == "${FFMPEG_EXPECTED_TAG}" ]]; then
    log "Minimal FFmpeg ${FFMPEG_VERSION} already built for ${FFMPEG_EXPECTED_TAG}, skipping."
else
    [[ -f "${FFMPEG_MARKER}" ]] && {
        log "FFmpeg cache stale (target or config changed) — rebuilding ..."
        rm -rf "${FFMPEG_INSTALL}"
    }

    log "Building minimal FFmpeg ${FFMPEG_VERSION} from source ..."
    download_source "${FFMPEG_URL}" "${DEPS_SRC}/${FFMPEG_TARBALL}"

    if [[ ! -d "${FFMPEG_SRC_DIR}" ]]; then
        log "Extracting FFmpeg source ..."
        tar -xf "${DEPS_SRC}/${FFMPEG_TARBALL}" -C "${DEPS_SRC}"
    fi

    pushd "${FFMPEG_SRC_DIR}" > /dev/null

    log "Configuring minimal FFmpeg (target: ${GLIBC_TARGET}) ..."
    ./configure \
        --prefix="${FFMPEG_INSTALL}" \
        --cc="${ZIG_CC}" \
        --cxx="${ZIG_CXX}" \
        --ld="${ZIG_CC}" \
        --extra-cflags="-I${OPENSSL_INSTALL}/include" \
        --extra-ldflags="-L${OPENSSL_INSTALL}/lib" \
        --disable-everything \
        --enable-shared \
        --disable-static \
        --enable-pic \
        --enable-avformat \
        --enable-avcodec \
        --enable-avutil \
        --enable-swscale \
        --enable-swresample \
        --enable-network \
        --enable-openssl \
        --enable-protocol=file \
        --enable-protocol=http \
        --enable-protocol=https \
        --enable-protocol=tcp \
        --enable-protocol=tls \
        --enable-demuxer=mov \
        --enable-demuxer=matroska \
        --enable-decoder=h264 \
        --enable-decoder=hevc \
        --enable-parser=h264 \
        --enable-parser=hevc \
        --enable-bsf=h264_mp4toannexb \
        --enable-bsf=hevc_mp4toannexb \
        --disable-doc \
        --disable-programs \
        --disable-vdpau

    # zig cc bundles glibc headers that include sys/sysctl.h, so configure
    # incorrectly sets HAVE_SYSCTL=1. On Linux the sysctl syscall is deprecated
    # and modern distros no longer ship sys/sysctl.h. Fix before compiling.
    sed -i 's/^#define HAVE_SYSCTL 1$/#define HAVE_SYSCTL 0/' config.h

    log "Compiling minimal FFmpeg ($(nproc) jobs) ..."
    make -j"$(nproc)"

    log "Installing minimal FFmpeg to ${FFMPEG_INSTALL} ..."
    # Same tmpfs staging trick with cp -r (no chmod).
    _ffmpeg_tmp="$(mktemp -d)"
    make install-libs install-headers DESTDIR="${_ffmpeg_tmp}"
    mkdir -p "${FFMPEG_INSTALL}"
    cp -r "${_ffmpeg_tmp}${FFMPEG_INSTALL}/." "${FFMPEG_INSTALL}/"
    rm -rf "${_ffmpeg_tmp}"

    echo "${FFMPEG_EXPECTED_TAG}" > "${FFMPEG_TOOLCHAIN_MARKER}"
    popd > /dev/null
    log "Minimal FFmpeg build complete."
fi

# Point pkg-config at our source-built OpenSSL and FFmpeg so cmake's
# find_package / pkg_check_modules pick up the correct versions.
# LD_LIBRARY_PATH is NOT set globally here — it would cause cmake itself to
# load our custom OpenSSL 3.4.1 instead of the system one, breaking cmake's
# own libcurl/ngtcp2 which expect a newer OpenSSL. It is set only inline for
# the linuxdeploy invocation below.
export PKG_CONFIG_PATH="${OPENSSL_INSTALL}/lib/pkgconfig:${FFMPEG_INSTALL}/lib/pkgconfig:${PKG_CONFIG_PATH:-}"

# ---------------------------------------------------------------------------
# Download AppImage toolchain (cached in appimage-tools/)
# ---------------------------------------------------------------------------
download_tool "${LINUXDEPLOY_URL}"  "${TOOLS_DIR}/${LINUXDEPLOY_BIN}"
download_tool "${APPIMAGETOOL_URL}" "${TOOLS_DIR}/${APPIMAGETOOL_BIN}"

APPIMAGETOOL="${TOOLS_DIR}/${APPIMAGETOOL_BIN}"

# linuxdeploy is run from an extracted directory rather than as an AppImage.
# Its bundled strip is too old to handle .relr.dyn sections (RELR relocations)
# present in modern distro libs — it exits non-zero and aborts packaging.
# Extracting lets us swap in the system strip.
LINUXDEPLOY_DIR="${TOOLS_DIR}/linuxdeploy-extracted"
if [[ ! -d "${LINUXDEPLOY_DIR}" ]]; then
    log "Extracting linuxdeploy (one-time setup) ..."
    pushd "${TOOLS_DIR}" > /dev/null
    "${TOOLS_DIR}/${LINUXDEPLOY_BIN}" --appimage-extract > /dev/null
    mv squashfs-root "${LINUXDEPLOY_DIR}"
    popd > /dev/null
fi
cp "$(command -v strip)" "${LINUXDEPLOY_DIR}/usr/bin/strip"
LINUXDEPLOY="${LINUXDEPLOY_DIR}/AppRun"

# ---------------------------------------------------------------------------
# Compile the glibc compatibility shim
#
# glibc_compat.c defines wrapper functions for the ~11 glibc symbols that
# g++ on Arch would otherwise bind to GLIBC_2.38 or GLIBC_2.43:
#   - sqrtf, acosf, atan2f, fmod, fmodf   (math, GLIBC_2.43)
#   - __isoc23_strtol/ll/ul/ull           (C23 strtol variants, GLIBC_2.38)
#   - __isoc23_fscanf, __isoc23_sscanf    (C23 scanf variants, GLIBC_2.38)
#
# Each wrapper delegates to an old-version binding via .symver, so the final
# ELF only records GLIBC_2.2.5 for these symbols.
#
# This also intercepts unversioned references from pre-built archives
# (libsioclient_tls.a) that the linker would bind to the host's default
# (current) glibc version. By providing our own sqrtf/etc. definitions,
# those references are satisfied by our wrappers rather than libm directly.
#
# Compile flags:
#   -std=c11     disables C23 header redirections (strtol → __isoc23_strtol)
#   -fno-builtin prevents GCC from replacing calls with inline SSE/AVX
#   -O1          minimal optimisation — enough to avoid bloat
# ---------------------------------------------------------------------------
log "Compiling glibc compatibility shim ..."
mkdir -p "${BUILD_DIR}"
gcc -std=c11 -fno-builtin -O1 -c \
    -o "${BUILD_DIR}/glibc_compat.o" \
    "${SCRIPT_DIR}/glibc_compat.c"

# ---------------------------------------------------------------------------
# Build infinidream (Release) with system g++
#
# System g++ is used (not zig) because zig c++ uses libc++ (LLVM ABI) while
# libsioclient_tls.a was compiled with libstdc++ (GNU ABI) — the two are
# incompatible.  The glibc_compat.o (pre-compiled above) is injected via
# CMAKE_EXE_LINKER_FLAGS so it is linked into the final binary before -lm/-lc,
# intercepting all symbol version bindings.
# ---------------------------------------------------------------------------
log "Configuring CMake (system g++, static Boost, glibc shim) ..."
cmake -B "${BUILD_DIR}" -S "${SCRIPT_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="gcc" \
    -DCMAKE_CXX_COMPILER="g++" \
    -DOPENSSL_ROOT_DIR="${OPENSSL_INSTALL}" \
    -DBoost_USE_STATIC_LIBS=ON \
    -DCMAKE_EXE_LINKER_FLAGS="${BUILD_DIR}/glibc_compat.o -lm -L${OPENSSL_INSTALL}/lib -L${FFMPEG_INSTALL}/lib -Wl,--allow-shlib-undefined"

log "Compiling ($(nproc) jobs) ..."
cmake --build "${BUILD_DIR}" -j"$(nproc)"

# ---------------------------------------------------------------------------
# Verify glibc version ceiling before packaging
#
# Fail fast if the binary references anything newer than GLIBC_2.35, so we
# catch regressions before producing and shipping a broken AppImage.
# ---------------------------------------------------------------------------
log "Verifying glibc symbol version ceiling ..."
BAD_SYMS="$(readelf -sW "${BUILD_DIR}/infinidream" 2>/dev/null \
    | awk '$7 ~ /GLIBC_2\.(3[6-9]|[4-9][0-9])/ {print $7, $8}' \
    | sort -u)" || true
if [[ -n "${BAD_SYMS}" ]]; then
    echo ""
    echo "ERROR: Binary references glibc symbols newer than 2.35:"
    echo "${BAD_SYMS}"
    echo ""
    echo "The AppImage would fail on Ubuntu 22.04 / glibc 2.35."
    echo "Check glibc_compat.c and add wrappers for the above symbols."
    exit 1
fi
log "glibc ceiling check passed — no symbols newer than GLIBC_2.35 found."

# ---------------------------------------------------------------------------
# Assemble AppDir
# ---------------------------------------------------------------------------
log "Assembling AppDir ..."
mkdir -p "${APPDIR}/usr/bin"
mkdir -p "${APPDIR}/usr/lib"

cp "${BUILD_DIR}/infinidream"    "${APPDIR}/usr/bin/"
cp -r "${BUILD_DIR}/shaders"     "${APPDIR}/usr/bin/"

# Runtime assets (logo, font, OSD PNGs) next to the binary so SHAREDIR="./"
# resolves correctly when AppRun cd's into usr/bin/
cp "${RUNTIME_DIR}/"*.png        "${APPDIR}/usr/bin/"
cp "${RUNTIME_DIR}/Lato-Regular.ttf" "${APPDIR}/usr/bin/"

# AppImage mandatory files
cp "${SCRIPT_DIR}/AppRun"              "${APPDIR}/AppRun"
chmod +x "${APPDIR}/AppRun"
cp "${SCRIPT_DIR}/infinidream.desktop" "${APPDIR}/"
cp "${RUNTIME_DIR}/logo.png"           "${APPDIR}/infinidream.png"

# ---------------------------------------------------------------------------
# Bundle shared library dependencies with linuxdeploy
#
# Exclusion policy for a Vulkan application:
#   libvulkan     — ICD loader, must come from host (driver-specific)
#   libGL/libEGL  — GPU driver libs, must come from host
#   libX11/xcb    — X11 display protocol, always present on X11 hosts
#   libwayland-*  — Wayland protocol, present when Wayland runs
#   libxkbcommon  — Input, available on any desktop
#   libdecor      — Wayland decoration helper, host-provided
#   libm/libc     — glibc parts, must match host (cannot bundle glibc)
#
#   libvdpau      — VDPAU hardware-decode; disabled in FFmpeg build so avutil no
#                   longer NEEDS it; excluded here as belt-and-suspenders
#   libgssapi/krb5 family — Kerberos, pulled in by libcurl's GSSAPI support;
#                   built-in Arch versions need GLIBC_2.38; rely on host libs
#                   (libgssapi-krb5-2 is installed by default on Ubuntu 22.04)
#
# OpenSSL and FFmpeg are source-built and glibc-2.35 clean; they are bundled.
# Boost is static-linked into the binary (Boost_USE_STATIC_LIBS=ON), but
# Arch's Boost was compiled with ICU support so the static libs still carry
# dynamic references to libicu{uc,i18n,data}. These appear directly in the
# binary's NEEDED section and must be bundled — the host (e.g. Ubuntu 22.04)
# will have a different ICU major version and cannot satisfy them. Arch's ICU
# libs have no versioned GLIBC_ symbols, so bundling them is safe.
# ---------------------------------------------------------------------------
log "Running linuxdeploy to bundle shared libraries ..."
LD_LIBRARY_PATH="${OPENSSL_INSTALL}/lib:${FFMPEG_INSTALL}/lib:${LD_LIBRARY_PATH:-}" \
"${LINUXDEPLOY}" \
    --appdir "${APPDIR}" \
    --executable "${APPDIR}/usr/bin/infinidream" \
    --exclude-library "libvulkan.so*"         \
    --exclude-library "libGL.so*"             \
    --exclude-library "libGLX.so*"            \
    --exclude-library "libGLdispatch.so*"     \
    --exclude-library "libEGL.so*"            \
    --exclude-library "libX11.so*"            \
    --exclude-library "libX11-xcb.so*"        \
    --exclude-library "libXau.so*"            \
    --exclude-library "libXdmcp.so*"          \
    --exclude-library "libxcb.so*"            \
    --exclude-library "libxcb-*.so*"          \
    --exclude-library "libXrender.so*"        \
    --exclude-library "libXext.so*"           \
    --exclude-library "libwayland-client.so*" \
    --exclude-library "libwayland-cursor.so*" \
    --exclude-library "libwayland-egl.so*"    \
    --exclude-library "libxkbcommon.so*"      \
    --exclude-library "libdecor-0.so*"        \
    --exclude-library "libpthread.so*"        \
    --exclude-library "libm.so*"              \
    --exclude-library "libc.so*"              \
    --exclude-library "libdl.so*"             \
    --exclude-library "librt.so*"             \
    \
    --exclude-library "libvdpau.so*"          \
    \
    --exclude-library "libgssapi_krb5.so*"    \
    --exclude-library "libkrb5.so*"           \
    --exclude-library "libkrb5support.so*"    \
    --exclude-library "libk5crypto.so*"       \
    --exclude-library "libkeyutils.so*"       \

# ---------------------------------------------------------------------------
# Create the AppImage
# ---------------------------------------------------------------------------
log "Creating AppImage ..."
rm -f "${OUTPUT_APPIMAGE}"
"${APPIMAGETOOL}" "${APPDIR}" "${OUTPUT_APPIMAGE}"

log ""
log "Done! AppImage created at:"
log "  ${OUTPUT_APPIMAGE}"
log ""
log "Run with:                   ${OUTPUT_APPIMAGE}"
log "Run without FUSE:           APPIMAGE_EXTRACT_AND_RUN=1 ${OUTPUT_APPIMAGE}"
