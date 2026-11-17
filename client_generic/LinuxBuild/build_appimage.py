#!/usr/bin/env python3
"""
infinidream Linux AppImage Build Script

Builds infinidream and packages it as a self-contained AppImage.

Usage:
    cd client/client_generic/LinuxBuild
    ./build_appimage.py                  # prod, dev build versioned from the latest git tag
    ./build_appimage.py --stage          # stage (api-stage.infinidream.ai)
    ./build_appimage.py -v 0.16.2 -g     # release build, uploaded to GitHub release 0.16.2

Output: infinidream-<arch>.AppImage (or infinidream-stage-<arch>.AppImage)
in this directory.

Requirements:
  - Build tools: cmake, gcc, g++, make, perl
  - curl / Python urllib  (downloads sources + linuxdeploy / appimagetool on first run)
  - zig         (used ONLY to compile OpenSSL, libcurl and FFmpeg against glibc 2.35;
                 the main binary uses system g++)
                 Install: pacman -S zig  OR  apt install zig
  - nasm/yasm   (FFmpeg assembly optimisations; skipped gracefully if absent)
  - Vulkan headers >= 1.3.277 (FFmpeg Vulkan Video decode)
  - FUSE or kernel >= 5.13 with /dev/fuse available (for running AppImages).
    The script sets APPIMAGE_EXTRACT_AND_RUN=1 automatically without FUSE.
"""

import argparse
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request
from pathlib import Path
from typing import Optional


# ---------------------------------------------------------------------------
# ANSI colours
# ---------------------------------------------------------------------------
class Colors:
    RED    = '\033[0;31m'
    GREEN  = '\033[0;32m'
    YELLOW = '\033[1;33m'
    BLUE   = '\033[0;34m'
    NC     = '\033[0m'


def log(message: str) -> None:
    print(f"{Colors.BLUE}[build]{Colors.NC} {message}")


def die(message: str) -> None:
    print(f"{Colors.RED}ERROR:{Colors.NC} {message}", file=sys.stderr)
    sys.exit(1)


# ---------------------------------------------------------------------------
# Shell helpers
# ---------------------------------------------------------------------------
def run(cmd: list[str], *, check: bool = True, env: Optional[dict] = None,
        cwd: Optional[Path] = None) -> subprocess.CompletedProcess:
    merged = os.environ.copy()
    if env:
        merged.update(env)
    return subprocess.run(cmd, check=check, text=True, env=merged, cwd=cwd)


def capture(cmd: list[str], *, env: Optional[dict] = None) -> str:
    merged = os.environ.copy()
    if env:
        merged.update(env)
    result = subprocess.run(cmd, check=False, capture_output=True, text=True, env=merged)
    return result.stdout.strip()


def require_cmd(name: str) -> None:
    if not shutil.which(name):
        die(f"'{name}' not found. Please install it.")


# ---------------------------------------------------------------------------
# Download helpers
# ---------------------------------------------------------------------------
def download_file(url: str, dest: Path) -> None:
    """Download url → dest with a simple progress indicator."""
    log(f"Downloading {dest.name} ...")

    def _reporthook(block_num, block_size, total_size):
        if total_size > 0:
            pct = min(100, block_num * block_size * 100 // total_size)
            print(f"\r  {pct}%", end="", flush=True)

    urllib.request.urlretrieve(url, dest, reporthook=_reporthook)
    print()  # newline after progress


def download_tool(url: str, dest: Path) -> None:
    if dest.exists() and os.access(dest, os.X_OK):
        log(f"{dest.name} already present, skipping download.")
        return
    download_file(url, dest)
    dest.chmod(0o755)


def download_source(url: str, dest: Path) -> None:
    if dest.exists():
        log(f"{dest.name} already downloaded, skipping.")
        return
    download_file(url, dest)


# ---------------------------------------------------------------------------
# Cache validity
# ---------------------------------------------------------------------------
def cache_valid(marker: Path, toolchain_marker: Path, expected_tag: str) -> bool:
    return (marker.exists()
            and toolchain_marker.exists()
            and toolchain_marker.read_text().strip() == expected_tag)


# ---------------------------------------------------------------------------
# Build constants
# ---------------------------------------------------------------------------
ARCH = platform.machine()          # e.g. "x86_64"
SCRIPT_DIR = Path(__file__).resolve().parent

# glibc ABI ceiling. Every file in the AppImage must stay at or below it:
# see verify_abi_ceiling().
#
# OpenSSL, libcurl and FFmpeg are compiled as shared libraries with zig cc
# targeting this glibc version, so the bundled .so files don't reference any
# symbol newer than GLIBC_2.35.
#
# The main infinidream binary is compiled with system g++, with libstdc++ and
# libgcc linked statically. The glibc_compat.c shim intercepts the ~11 symbols
# that g++ would otherwise bind to newer glibc versions (GLIBC_2.38 /
# GLIBC_2.43) and redirects them to GLIBC_2.2.5 via .symver directives.
#
# Net result: the AppImage runs on glibc >= 2.35, which covers Ubuntu 22.04
# LTS, Debian 12, Fedora 36+ and Linux Mint 21+.
GLIBC_CEILING = (2, 35)
GLIBC_TARGET = f"{ARCH}-linux-gnu.{GLIBC_CEILING[0]}.{GLIBC_CEILING[1]}"

# Newest symbol versions allowed per library ABI. libstdc++ and libgcc are
# linked statically, so nothing should need them at all; the GLIBCXX/CXXABI
# caps (GCC 12's libstdc++, as on Ubuntu 22.04 / Debian 12) catch a bundled
# or host libstdc++ dependency sneaking back in.
ABI_CEILINGS = {
    "GLIBC":   GLIBC_CEILING,
    "GLIBCXX": (3, 4, 30),
    "CXXABI":  (1, 3, 13),
}

BUILD_DIR    = SCRIPT_DIR / "build-appimage"
APPDIR       = SCRIPT_DIR / "AppDir"
TOOLS_DIR    = SCRIPT_DIR / "appimage-tools"
DEPS_SRC     = SCRIPT_DIR / "deps-src"
RUNTIME_DIR  = SCRIPT_DIR.parent / "Runtime"

# Dependency versions (pinned for reproducibility)
OPENSSL_VERSION  = "3.5.0"
OPENSSL_TARBALL  = f"openssl-{OPENSSL_VERSION}.tar.gz"
OPENSSL_URL      = (f"https://github.com/openssl/openssl/releases/download/"
                    f"openssl-{OPENSSL_VERSION}/{OPENSSL_TARBALL}")
OPENSSL_SRC_DIR  = DEPS_SRC / f"openssl-{OPENSSL_VERSION}"
OPENSSL_INSTALL  = SCRIPT_DIR / "openssl-minimal"

# The distro libcurl drags in LDAP, RTMP, SSH, SASL, PSL and GnuTLS, all of
# which need the build host's (newer) glibc (#807). We only need HTTP(S).
CURL_VERSION     = "8.23.0"
CURL_TARBALL     = f"curl-{CURL_VERSION}.tar.xz"
CURL_URL         = f"https://curl.se/download/{CURL_TARBALL}"
CURL_SRC_DIR     = DEPS_SRC / f"curl-{CURL_VERSION}"
CURL_INSTALL     = SCRIPT_DIR / "curl-minimal"

# Bump when libcurl configure flags change — forces a clean rebuild
CURL_CONFIG_TAG  = "http-openssl-cafallback"

# 9.x reuses a staging buffer for Vulkan frame downloads on NVIDIA; 8.x
# imports (allocates and frees) GPU memory for every decoded frame there.
# Windows/macOS stay on vcpkg's FFmpeg — they never download Vulkan frames.
FFMPEG_VERSION   = "9.0.2"
FFMPEG_TARBALL   = f"ffmpeg-{FFMPEG_VERSION}.tar.xz"
FFMPEG_URL       = f"https://ffmpeg.org/releases/{FFMPEG_TARBALL}"
FFMPEG_SRC_DIR   = DEPS_SRC / f"ffmpeg-{FFMPEG_VERSION}"
FFMPEG_INSTALL   = SCRIPT_DIR / "ffmpeg-minimal"

# Bump when FFmpeg configure flags change — forces a clean rebuild
FFMPEG_CONFIG_TAG = "hevc-h264-mp4-nonet-vulkan"

LINUXDEPLOY_BIN  = f"linuxdeploy-{ARCH}.AppImage"
APPIMAGETOOL_BIN = f"appimagetool-{ARCH}.AppImage"

LINUXDEPLOY_URL  = (f"https://github.com/linuxdeploy/linuxdeploy/releases/"
                    f"download/continuous/{LINUXDEPLOY_BIN}")
APPIMAGETOOL_URL = (f"https://github.com/AppImage/appimagetool/releases/"
                    f"download/continuous/{APPIMAGETOOL_BIN}")

OUTPUT_APPIMAGE  = SCRIPT_DIR / f"infinidream-{ARCH}.AppImage"


# ---------------------------------------------------------------------------
# Preflight
# ---------------------------------------------------------------------------
def preflight() -> None:
    for cmd in ("cmake", "gcc", "g++", "curl", "make", "perl", "zig"):
        require_cmd(cmd)

    if not Path("/dev/fuse").exists():
        log("WARNING: /dev/fuse not found — enabling APPIMAGE_EXTRACT_AND_RUN=1 for tooling.")
        os.environ["APPIMAGE_EXTRACT_AND_RUN"] = "1"


# ---------------------------------------------------------------------------
# zig cc / zig c++ wrapper scripts
# ---------------------------------------------------------------------------
def write_zig_wrappers() -> tuple[Path, Path]:
    """
    Create thin shell wrappers that invoke 'zig cc/c++' with the glibc target.

    zig cc wraps clang and enforces a hard glibc ABI ceiling at compile+link
    time via --target.  These wrappers are used ONLY for OpenSSL and FFmpeg.
    The main infinidream binary is compiled with system g++.
    """
    zig_cc  = TOOLS_DIR / "zig-cc"
    zig_cxx = TOOLS_DIR / "zig-cxx"

    zig_cc.write_text(
        f"#!/bin/sh\n"
        f'exec zig cc -target {GLIBC_TARGET} -isystem /usr/include "$@" -L/usr/lib\n'
    )
    zig_cc.chmod(0o755)

    zig_cxx.write_text(
        f"#!/bin/sh\n"
        f'exec zig c++ -target {GLIBC_TARGET} -isystem /usr/include "$@" -L/usr/lib\n'
    )
    zig_cxx.chmod(0o755)

    glibc_ver = GLIBC_TARGET.split("gnu.")[-1]
    log(f"Dependency compiler wrappers target glibc {glibc_ver}")
    return zig_cc, zig_cxx


# ---------------------------------------------------------------------------
# Build OpenSSL from source (cached)
# ---------------------------------------------------------------------------
def build_openssl(zig_cc: Path, zig_cxx: Path) -> None:
    """
    Build OpenSSL from source with zig wrappers so the resulting .so files
    don't reference any symbol newer than GLIBC_2.35.

    Arch's system OpenSSL 3.6+ references __isoc23_strtol@GLIBC_2.38.
    Bundling it would break on Ubuntu 22.04.
    """
    marker           = OPENSSL_INSTALL / "lib" / "pkgconfig" / "openssl.pc"
    toolchain_marker = OPENSSL_INSTALL / ".toolchain-target"

    if cache_valid(marker, toolchain_marker, GLIBC_TARGET):
        log(f"OpenSSL {OPENSSL_VERSION} already built for {GLIBC_TARGET}, skipping.")
        return

    if marker.exists():
        log("OpenSSL cache exists but for a different target — rebuilding ...")
        shutil.rmtree(OPENSSL_INSTALL)

    log(f"Building OpenSSL {OPENSSL_VERSION} from source ...")
    tarball = DEPS_SRC / OPENSSL_TARBALL
    download_source(OPENSSL_URL, tarball)

    if not OPENSSL_SRC_DIR.exists():
        log("Extracting OpenSSL source ...")
        with tarfile.open(tarball) as tf:
            tf.extractall(DEPS_SRC)

    env = {"CC": str(zig_cc), "CXX": str(zig_cxx)}

    log(f"Configuring OpenSSL {OPENSSL_VERSION} ...")
    run([
        str(OPENSSL_SRC_DIR / "Configure"),
        f"--prefix={OPENSSL_INSTALL}",
        f"--openssldir={OPENSSL_INSTALL}/etc/ssl",
        "--libdir=lib",
        "linux-x86_64",
        "shared",
        "no-zlib",
        "no-tests",
        f"CC={zig_cc}",
        f"CXX={zig_cxx}",
    ], cwd=OPENSSL_SRC_DIR)

    nproc = os.cpu_count() or 1
    log(f"Compiling OpenSSL libraries ({nproc} jobs) ...")
    run(["make", f"-j{nproc}", "build_libs"], cwd=OPENSSL_SRC_DIR)

    log(f"Installing OpenSSL to {OPENSSL_INSTALL} ...")
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        run(["make", "install_dev", f"DESTDIR={tmp}"], cwd=OPENSSL_SRC_DIR)
        # cp -r avoids chmod (works on filesystems that reject permission changes)
        OPENSSL_INSTALL.mkdir(parents=True, exist_ok=True)
        run(["cp", "-r", f"{tmp}{OPENSSL_INSTALL}/.", str(OPENSSL_INSTALL)])

    toolchain_marker.write_text(GLIBC_TARGET)
    log("OpenSSL build complete.")


# ---------------------------------------------------------------------------
# Build minimal libcurl from source (cached)
# ---------------------------------------------------------------------------
def build_curl(zig_cc: Path, zig_cxx: Path) -> None:
    """
    Build an HTTP/HTTPS-only libcurl against openssl-minimal with the zig
    wrappers, so it and its dependencies stay glibc-2.35 clean (#807).

    No CA bundle path is compiled in (the build host's path may not exist on
    the user's distro). --with-ca-fallback makes libcurl use OpenSSL's default
    verify paths, which honour SSL_CERT_FILE; AppRun points that at the
    host's CA bundle.
    """
    marker           = CURL_INSTALL / "lib" / "pkgconfig" / "libcurl.pc"
    toolchain_marker = CURL_INSTALL / ".toolchain-target"
    expected_tag     = f"{GLIBC_TARGET}-{CURL_VERSION}-{OPENSSL_VERSION}-{CURL_CONFIG_TAG}"

    if cache_valid(marker, toolchain_marker, expected_tag):
        log(f"Minimal libcurl {CURL_VERSION} already built for {expected_tag}, skipping.")
        return

    if CURL_INSTALL.exists():
        log("libcurl cache stale (target or config changed) — rebuilding ...")
        shutil.rmtree(CURL_INSTALL)

    log(f"Building minimal libcurl {CURL_VERSION} from source ...")
    tarball = DEPS_SRC / CURL_TARBALL
    download_source(CURL_URL, tarball)

    if CURL_SRC_DIR.exists():
        # A previous configure may have cached results for other flags.
        shutil.rmtree(CURL_SRC_DIR)
    log("Extracting libcurl source ...")
    with tarfile.open(tarball) as tf:
        tf.extractall(DEPS_SRC)

    log(f"Configuring minimal libcurl (target: {GLIBC_TARGET}) ...")
    run([
        str(CURL_SRC_DIR / "configure"),
        f"--prefix={CURL_INSTALL}",
        "--enable-shared",
        "--disable-static",
        f"--with-openssl={OPENSSL_INSTALL}",
        "--with-ca-fallback",
        "--without-ca-bundle",
        "--without-ca-path",
        # HTTP(S) and HTTP proxies only (Networking.cpp supports a proxy).
        "--disable-ftp", "--disable-file", "--disable-ldap", "--disable-ldaps",
        "--disable-rtsp", "--disable-dict", "--disable-telnet", "--disable-tftp",
        "--disable-pop3", "--disable-imap", "--disable-smb", "--disable-smtp",
        "--disable-gopher", "--disable-mqtt", "--disable-ipfs",
        "--disable-websockets",
        "--disable-manual", "--disable-docs",
        "--without-gnutls", "--without-libpsl", "--without-libidn2",
        "--without-librtmp", "--without-libssh2", "--without-libssh",
        "--without-nghttp2", "--without-ngtcp2", "--without-nghttp3",
        "--without-brotli", "--without-zstd", "--without-zlib",
        "--without-libgsasl", "--without-gssapi",
        f"CC={zig_cc}",
    ], cwd=CURL_SRC_DIR)

    nproc = os.cpu_count() or 1
    log(f"Compiling minimal libcurl ({nproc} jobs) ...")
    run(["make", f"-j{nproc}", "-C", "lib"], cwd=CURL_SRC_DIR)

    log(f"Installing minimal libcurl to {CURL_INSTALL} ...")
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        for sub in ("lib", "include"):
            run(["make", "-C", sub, "install", f"DESTDIR={tmp}"], cwd=CURL_SRC_DIR)
        run(["make", "install-pkgconfigDATA", f"DESTDIR={tmp}"], cwd=CURL_SRC_DIR)
        CURL_INSTALL.mkdir(parents=True, exist_ok=True)
        run(["cp", "-r", f"{tmp}{CURL_INSTALL}/.", str(CURL_INSTALL)])

    toolchain_marker.write_text(expected_tag)
    log("Minimal libcurl build complete.")


# ---------------------------------------------------------------------------
# Build minimal FFmpeg from source (cached)
# ---------------------------------------------------------------------------
def build_ffmpeg(zig_cc: Path, zig_cxx: Path) -> None:
    """
    Build a minimal FFmpeg aligned with overlays/ffmpeg (#556):
    H.264/HEVC decode with Vulkan Video, mov demux, file protocol only. No
    network/TLS/audio — libcurl owns HTTPS (ProgressiveHttpStream).

    Uses the zig wrappers so bundled .so files are glibc-2.35 clean.
    """
    marker           = FFMPEG_INSTALL / "lib" / "pkgconfig" / "libavcodec.pc"
    toolchain_marker = FFMPEG_INSTALL / ".toolchain-target"
    expected_tag     = f"{GLIBC_TARGET}-{FFMPEG_VERSION}-{FFMPEG_CONFIG_TAG}"

    if cache_valid(marker, toolchain_marker, expected_tag):
        log(f"Minimal FFmpeg {FFMPEG_VERSION} already built for {expected_tag}, skipping.")
        return

    if marker.exists():
        log("FFmpeg cache stale (target or config changed) — rebuilding ...")
        shutil.rmtree(FFMPEG_INSTALL)

    log(f"Building minimal FFmpeg {FFMPEG_VERSION} from source ...")
    tarball = DEPS_SRC / FFMPEG_TARBALL
    download_source(FFMPEG_URL, tarball)

    if not FFMPEG_SRC_DIR.exists():
        log("Extracting FFmpeg source ...")
        with tarfile.open(tarball) as tf:
            tf.extractall(DEPS_SRC)

    nproc = os.cpu_count() or 1
    log(f"Configuring minimal FFmpeg (target: {GLIBC_TARGET}) ...")
    run([
        str(FFMPEG_SRC_DIR / "configure"),
        f"--prefix={FFMPEG_INSTALL}",
        f"--cc={zig_cc}",
        f"--cxx={zig_cxx}",
        f"--ld={zig_cc}",
        "--disable-everything",
        "--enable-shared",
        "--disable-static",
        "--enable-pic",
        "--enable-avformat",
        "--enable-avcodec",
        "--enable-avutil",
        "--enable-swscale",
        "--enable-decoder=h264",
        "--enable-decoder=hevc",
        "--enable-parser=h264",
        "--enable-parser=hevc",
        "--enable-bsf=h264_mp4toannexb",
        "--enable-bsf=hevc_mp4toannexb",
        "--enable-demuxer=mov",
        "--enable-protocol=file",
        "--disable-network",
        # Vulkan Video hardware decode. FFmpeg loads libvulkan at runtime, so
        # this adds no link dependency; it needs Vulkan headers >= 1.3.277 at
        # build time.
        "--enable-vulkan",
        "--enable-hwaccel=h264_vulkan",
        "--enable-hwaccel=hevc_vulkan",
        "--disable-doc",
        "--disable-programs",
        "--disable-vdpau",
        # VAAPI is unused; if autodetected it would make libva a hard runtime
        # dependency of the AppImage.
        "--disable-vaapi",
    ], cwd=FFMPEG_SRC_DIR)

    # --enable-hwaccel silently drops hwaccels whose dependencies are missing,
    # so check they made it in rather than shipping software-only decode.
    components = (FFMPEG_SRC_DIR / "config_components.h").read_text()
    for define in ("CONFIG_H264_VULKAN_HWACCEL", "CONFIG_HEVC_VULKAN_HWACCEL"):
        if f"#define {define} 1" not in components:
            die(f"FFmpeg configure did not enable {define} (are the Vulkan "
                f"headers >= 1.3.277 installed?)")

    # zig cc bundles glibc headers that include sys/sysctl.h, so configure
    # incorrectly sets HAVE_SYSCTL=1.  Fix before compiling.
    config_h = FFMPEG_SRC_DIR / "config.h"
    text = config_h.read_text()
    config_h.write_text(text.replace(
        "#define HAVE_SYSCTL 1",
        "#define HAVE_SYSCTL 0",
    ))

    log(f"Compiling minimal FFmpeg ({nproc} jobs) ...")
    run(["make", f"-j{nproc}"], cwd=FFMPEG_SRC_DIR)

    log(f"Installing minimal FFmpeg to {FFMPEG_INSTALL} ...")
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        run(["make", "install-libs", "install-headers", f"DESTDIR={tmp}"],
            cwd=FFMPEG_SRC_DIR)
        FFMPEG_INSTALL.mkdir(parents=True, exist_ok=True)
        run(["cp", "-r", f"{tmp}{FFMPEG_INSTALL}/.", str(FFMPEG_INSTALL)])

    toolchain_marker.write_text(expected_tag)
    log("Minimal FFmpeg build complete.")


# ---------------------------------------------------------------------------
# Download and prepare AppImage toolchain
# ---------------------------------------------------------------------------
def prepare_appimage_tools() -> tuple[Path, Path]:
    """Download linuxdeploy + appimagetool; extract linuxdeploy and patch strip."""
    linuxdeploy_appimage = TOOLS_DIR / LINUXDEPLOY_BIN
    appimagetool         = TOOLS_DIR / APPIMAGETOOL_BIN

    download_tool(LINUXDEPLOY_URL,  linuxdeploy_appimage)
    download_tool(APPIMAGETOOL_URL, appimagetool)

    # linuxdeploy is run from an extracted directory rather than as an AppImage.
    # Its bundled strip is too old to handle .relr.dyn sections (RELR relocations)
    # present in modern distro libs.  Extracting lets us swap in the system strip.
    linuxdeploy_dir = TOOLS_DIR / "linuxdeploy-extracted"
    if not linuxdeploy_dir.exists():
        log("Extracting linuxdeploy (one-time setup) ...")
        run([str(linuxdeploy_appimage), "--appimage-extract"],
            cwd=TOOLS_DIR,
            env={"APPIMAGE_EXTRACT_AND_RUN": "1", **os.environ})
        (TOOLS_DIR / "squashfs-root").rename(linuxdeploy_dir)

    system_strip = shutil.which("strip")
    if system_strip:
        shutil.copy2(system_strip, linuxdeploy_dir / "usr" / "bin" / "strip")

    return linuxdeploy_dir / "AppRun", appimagetool


# ---------------------------------------------------------------------------
# Compile the glibc compatibility shim
# ---------------------------------------------------------------------------
def compile_glibc_shim() -> Path:
    """
    Compile glibc_compat.c — intercepts ~11 symbols that g++ on Arch would
    otherwise bind to GLIBC_2.38/2.43 and redirects them to GLIBC_2.2.5.
    """
    log("Compiling glibc compatibility shim ...")
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    shim_src = SCRIPT_DIR / "glibc_compat.c"
    shim_obj = BUILD_DIR / "glibc_compat.o"
    run([
        "gcc", "-std=c11", "-fno-builtin", "-O1", "-c",
        "-o", str(shim_obj),
        str(shim_src),
    ])
    return shim_obj


# ---------------------------------------------------------------------------
# CMake build
# ---------------------------------------------------------------------------
def build_infinidream(shim_obj: Path, stage: bool, version: Optional[str]) -> None:
    """
    Build infinidream (Release) with system g++.

    The glibc compat shim object is injected via CMAKE_EXE_LINKER_FLAGS to
    intercept versioned symbol bindings.
    """
    nproc = os.cpu_count() or 1

    linker_flags = (
        # Host libstdc++ on Ubuntu 22.04 / Debian 12 (GCC 12) lacks symbols
        # the build host's g++ emits (GLIBCXX_3.4.31+), so link it statically.
        f"-static-libstdc++ -static-libgcc "
        f"{shim_obj} -lm "
        f"-L{OPENSSL_INSTALL}/lib "
        f"-L{CURL_INSTALL}/lib "
        f"-L{FFMPEG_INSTALL}/lib "
        f"-Wl,--allow-shlib-undefined"
    )

    pkg_config_path = (
        f"{OPENSSL_INSTALL}/lib/pkgconfig:"
        f"{CURL_INSTALL}/lib/pkgconfig:"
        f"{FFMPEG_INSTALL}/lib/pkgconfig:"
        + os.environ.get("PKG_CONFIG_PATH", "")
    )

    log("Configuring CMake (system g++, static Boost, glibc shim) ...")
    run([
        "cmake",
        "-B", str(BUILD_DIR),
        "-S", str(SCRIPT_DIR),
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_C_COMPILER=gcc",
        "-DCMAKE_CXX_COMPILER=g++",
        f"-DOPENSSL_ROOT_DIR={OPENSSL_INSTALL}",
        # Point FindCURL at curl-minimal, not the system libcurl or its
        # CURLConfig.cmake.
        "-DCURL_NO_CURL_CMAKE=ON",
        f"-DCURL_INCLUDE_DIR={CURL_INSTALL}/include",
        f"-DCURL_LIBRARY={CURL_INSTALL}/lib/libcurl.so",
        "-DBoost_USE_STATIC_LIBS=ON",
        f"-DCMAKE_EXE_LINKER_FLAGS={linker_flags}",
        f"-DINFINIDREAM_STAGE={'ON' if stage else 'OFF'}",
    ] + ([f"-DAPP_VERSION={version}"] if version else []), env={"PKG_CONFIG_PATH": pkg_config_path, **os.environ})

    log(f"Compiling ({nproc} jobs) ...")
    run(["cmake", "--build", str(BUILD_DIR), f"-j{nproc}"])


# ---------------------------------------------------------------------------
# glibc ceiling check
# ---------------------------------------------------------------------------
def abi_requirements(path: Path) -> dict[str, tuple[int, ...]]:
    """Newest GLIBC_/GLIBCXX_/CXXABI_ version an ELF file needs, per ABI."""
    newest: dict[str, tuple[int, ...]] = {}
    # -V lists the version needs (.gnu.version_r), e.g. "Name: GLIBC_2.34".
    for part in capture(["readelf", "-VW", str(path)]).split():
        abi, sep, ver = part.partition("_")
        if not sep or abi not in ABI_CEILINGS:
            continue
        try:
            version = tuple(int(x) for x in ver.split("."))
        except ValueError:
            continue
        if version > newest.get(abi, ()):
            newest[abi] = version
    return newest


def fmt_version(abi: str, version: tuple[int, ...]) -> str:
    return f"{abi}_{'.'.join(map(str, version))}"


def verify_abi_ceiling(files: list[Path], what: str) -> None:
    """
    Fail if any of `files` needs a glibc or libstdc++ symbol version newer
    than ABI_CEILINGS allows. Catches regressions before producing a broken
    AppImage.
    """
    ceilings = ", ".join(fmt_version(a, v) for a, v in ABI_CEILINGS.items())
    log(f"Verifying ABI ceiling ({ceilings}) for {what} ...")

    bad = []
    for f in files:
        for abi, need in abi_requirements(f).items():
            if need > ABI_CEILINGS[abi]:
                bad.append((fmt_version(abi, need), f.name))

    if bad:
        print(f"\nERROR: {what} need symbol versions newer than allowed "
              f"({ceilings}):", file=sys.stderr)
        for need, name in sorted(bad):
            print(f"  {need}  {name}", file=sys.stderr)
        print("\nThe AppImage would fail on Ubuntu 22.04 / Debian 12.", file=sys.stderr)
        print("For the binary, check glibc_compat.c (GLIBC_) or the static "
              "libstdc++ link (GLIBCXX_/CXXABI_). For a bundled library, build "
              "it from source with the zig wrappers or exclude it.", file=sys.stderr)
        sys.exit(1)

    log(f"ABI ceiling check passed for {len(files)} file(s).")


# ---------------------------------------------------------------------------
# Assemble AppDir
# ---------------------------------------------------------------------------
def assemble_appdir() -> None:
    log("Assembling AppDir ...")
    (APPDIR / "usr" / "bin").mkdir(parents=True, exist_ok=True)
    (APPDIR / "usr" / "lib").mkdir(parents=True, exist_ok=True)

    shutil.copy2(BUILD_DIR / "infinidream", APPDIR / "usr" / "bin" / "infinidream")
    # PlatformUtils reads build metadata beside the executable. Keep the
    # packaged runtime banner consistent with an unpackaged Linux build.
    shutil.copy2(BUILD_DIR / "BuildData.json",
                 APPDIR / "usr" / "bin" / "BuildData.json")

    shaders_src = BUILD_DIR / "shaders"
    shaders_dst = APPDIR / "usr" / "bin" / "shaders"
    if shaders_dst.exists():
        shutil.rmtree(shaders_dst)
    shutil.copytree(shaders_src, shaders_dst)

    # Runtime assets (logo, font, OSD PNGs) sit next to the binary so
    # SHAREDIR="./" resolves correctly when AppRun cd's into usr/bin/
    for png in RUNTIME_DIR.glob("*.png"):
        shutil.copy2(png, APPDIR / "usr" / "bin" / png.name)
    shutil.copy2(RUNTIME_DIR / "Lato-Regular.ttf",
                 APPDIR / "usr" / "bin" / "Lato-Regular.ttf")

    # AppImage mandatory files
    apprun = SCRIPT_DIR / "AppRun"
    shutil.copy2(apprun, APPDIR / "AppRun")
    (APPDIR / "AppRun").chmod(0o755)
    shutil.copy2(SCRIPT_DIR / "infinidream.desktop", APPDIR / "infinidream.desktop")
    shutil.copy2(RUNTIME_DIR / "logo.png", APPDIR / "infinidream.png")


# ---------------------------------------------------------------------------
# Bundle dependencies with linuxdeploy
# ---------------------------------------------------------------------------
EXCLUDED_LIBS = [
    # GPU / display — must come from host
    "libvulkan.so*", "libGL.so*", "libGLX.so*", "libGLdispatch.so*", "libEGL.so*",
    # X11 / Wayland — always present on the host desktop
    "libX11.so*", "libX11-xcb.so*", "libXau.so*", "libXdmcp.so*",
    "libxcb.so*", "libxcb-*.so*",
    "libXrender.so*", "libXext.so*",
    "libwayland-client.so*", "libwayland-cursor.so*", "libwayland-egl.so*",
    "libxkbcommon.so*", "libdecor-0.so*",
    # libwayland's own dependency; a bundled copy needs the build host's glibc
    # and would shadow the host's for its libwayland (#807)
    "libffi.so*",
    # glibc — must match host
    "libpthread.so*", "libm.so*", "libc.so*", "libdl.so*", "librt.so*",
    # VDPAU — disabled in FFmpeg build; excluded as belt-and-suspenders
    "libvdpau.so*",
]


def bundle_libraries(linuxdeploy: Path) -> None:
    log("Running linuxdeploy to bundle shared libraries ...")

    exclude_args: list[str] = []
    for lib in EXCLUDED_LIBS:
        exclude_args += ["--exclude-library", lib]

    ld_library_path = (
        f"{OPENSSL_INSTALL}/lib:{CURL_INSTALL}/lib:{FFMPEG_INSTALL}/lib:"
        + os.environ.get("LD_LIBRARY_PATH", "")
    )

    run(
        [str(linuxdeploy),
         "--appdir", str(APPDIR),
         "--executable", str(APPDIR / "usr" / "bin" / "infinidream"),
         ] + exclude_args,
        env={"LD_LIBRARY_PATH": ld_library_path, **os.environ},
    )


# ---------------------------------------------------------------------------
# Unresolved symbol check
# ---------------------------------------------------------------------------
def verify_symbols_resolve() -> None:
    """
    Fail if any bundled library references a symbol nothing provides.

    The binary links with --allow-shlib-undefined, so a minimal FFmpeg that
    is missing an internal object still builds but cannot start.
    """
    log("Verifying all symbols resolve against the bundled libraries ...")
    result = subprocess.run(
        ["ldd", "-r", str(APPDIR / "usr" / "bin" / "infinidream")],
        capture_output=True, text=True,
        env={**os.environ, "LD_LIBRARY_PATH": str(APPDIR / "usr" / "lib")})
    bad = [line.strip() for line in (result.stdout + result.stderr).splitlines()
           if "undefined symbol" in line or "not found" in line]
    if bad:
        print("\nERROR: unresolved symbols in the AppDir:", file=sys.stderr)
        for line in bad:
            print(f"  {line}", file=sys.stderr)
        sys.exit(1)
    log("Symbol check passed.")


# ---------------------------------------------------------------------------
# Create the AppImage
# ---------------------------------------------------------------------------
def create_appimage(appimagetool: Path) -> None:
    log("Creating AppImage ...")
    OUTPUT_APPIMAGE.unlink(missing_ok=True)
    run([str(appimagetool), str(APPDIR), str(OUTPUT_APPIMAGE)])


# ---------------------------------------------------------------------------
# Version and GitHub release
# ---------------------------------------------------------------------------
def normalize_version(value: str) -> str:
    v = value.strip()
    if v[:1].lower() == "v" and len(v) > 1 and v[1].isdigit():
        v = v[1:]
    return v


def latest_git_tag() -> Optional[str]:
    tag = capture(["git", "-C", str(SCRIPT_DIR), "describe", "--tags", "--abbrev=0"])
    return normalize_version(tag) if tag else None


def check_github_release(tag: str) -> None:
    """Fail before the (long) build if the upload would fail."""
    require_cmd("gh")
    result = subprocess.run(["gh", "release", "view", tag, "--json", "tagName"],
                            capture_output=True, text=True, cwd=SCRIPT_DIR)
    if result.returncode != 0:
        die(f"GitHub release {tag} not found ({result.stderr.strip()}).\n"
            f"Create it first with the Mac build: "
            f"cd client_generic/MacBuild && ./build.py -r -n -g -v {tag}")


def github_upload(asset: Path, tag: str) -> None:
    log(f"Uploading {asset.name} to GitHub release {tag} ...")
    run(["gh", "release", "upload", tag, str(asset), "--clobber"], cwd=SCRIPT_DIR)
    log("Upload finished.")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main() -> None:
    global OUTPUT_APPIMAGE
    parser = argparse.ArgumentParser(description="Build the infinidream Linux AppImage.")
    parser.add_argument("--stage", action="store_true",
                        help="build against api-stage.infinidream.ai")
    parser.add_argument("-v", "--version",
                        help="release version X.Y.Z compiled into the app. Without it "
                             "the build is a dev build versioned from the latest git tag.")
    parser.add_argument("-g", "--github-release", action="store_true",
                        help="upload the AppImage to the GitHub release whose tag matches "
                             "--version (created by Mac build.py -g)")
    args = parser.parse_args()

    if args.github_release and not args.version:
        die("-g/--github-release needs -v/--version X.Y.Z to pick the release.")
    if args.version:
        version = normalize_version(args.version)
    else:
        version = latest_git_tag()
        if version:
            log(f"No -v given: dev build versioned from the latest git tag, {version}.")
        else:
            log("No -v given and no git tag found: the AppImage will report 0.0.0.")
    if args.github_release:
        check_github_release(version)
    if args.stage:
        OUTPUT_APPIMAGE = SCRIPT_DIR / f"infinidream-stage-{ARCH}.AppImage"

    # Clean AppDir and cmake build tree unconditionally at startup
    if APPDIR.exists():
        shutil.rmtree(APPDIR)
    if BUILD_DIR.exists():
        shutil.rmtree(BUILD_DIR)

    TOOLS_DIR.mkdir(parents=True, exist_ok=True)
    DEPS_SRC.mkdir(parents=True, exist_ok=True)

    preflight()

    zig_cc, zig_cxx = write_zig_wrappers()

    pkg_config_path = (
        f"{OPENSSL_INSTALL}/lib/pkgconfig:"
        f"{CURL_INSTALL}/lib/pkgconfig:"
        f"{FFMPEG_INSTALL}/lib/pkgconfig:"
        + os.environ.get("PKG_CONFIG_PATH", "")
    )
    os.environ["PKG_CONFIG_PATH"] = pkg_config_path

    build_openssl(zig_cc, zig_cxx)
    build_curl(zig_cc, zig_cxx)
    build_ffmpeg(zig_cc, zig_cxx)

    linuxdeploy, appimagetool = prepare_appimage_tools()

    shim_obj = compile_glibc_shim()
    build_infinidream(shim_obj, args.stage, version)
    verify_abi_ceiling([BUILD_DIR / "infinidream"], "the binary")

    assemble_appdir()
    bundle_libraries(linuxdeploy)
    verify_symbols_resolve()
    bundled = sorted(f for f in (APPDIR / "usr" / "lib").glob("*.so*")
                     if f.is_file() and not f.is_symlink())
    verify_abi_ceiling(bundled + [APPDIR / "usr" / "bin" / "infinidream"],
                         "bundled libraries")
    create_appimage(appimagetool)

    if args.github_release:
        github_upload(OUTPUT_APPIMAGE, version)

    print()
    log(f"Done! AppImage created at:")
    log(f"  {OUTPUT_APPIMAGE}")
    print()
    log(f"Run with:                   {OUTPUT_APPIMAGE}")
    log(f"Run without FUSE:           APPIMAGE_EXTRACT_AND_RUN=1 {OUTPUT_APPIMAGE}")


if __name__ == "__main__":
    main()
