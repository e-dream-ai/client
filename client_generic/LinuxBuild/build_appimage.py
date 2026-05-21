#!/usr/bin/env python3
"""
infinidream Linux AppImage Build Script

Builds infinidream and packages it as a self-contained AppImage.

Usage:
    cd client/client_generic/LinuxBuild
    ./build.py

Output: infinidream-<arch>.AppImage in this directory.

Requirements:
  - Build tools: cmake, gcc, g++, make, perl
  - curl / Python urllib  (downloads sources + linuxdeploy / appimagetool on first run)
  - zig         (used ONLY to compile OpenSSL and FFmpeg against glibc 2.35;
                 the main binary uses system g++ for compatibility with
                 the pre-built libsioclient_tls.a static archive)
                 Install: pacman -S zig  OR  apt install zig
  - nasm/yasm   (FFmpeg assembly optimisations; skipped gracefully if absent)
  - FUSE or kernel >= 5.13 with /dev/fuse available (for running AppImages).
    The script sets APPIMAGE_EXTRACT_AND_RUN=1 automatically without FUSE.
"""

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

# glibc ABI ceiling (see long comment in build_appimage.sh)
GLIBC_TARGET = f"{ARCH}-linux-gnu.2.35"

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

FFMPEG_VERSION   = "7.1.1"
FFMPEG_TARBALL   = f"ffmpeg-{FFMPEG_VERSION}.tar.xz"
FFMPEG_URL       = f"https://ffmpeg.org/releases/{FFMPEG_TARBALL}"
FFMPEG_SRC_DIR   = DEPS_SRC / f"ffmpeg-{FFMPEG_VERSION}"
FFMPEG_INSTALL   = SCRIPT_DIR / "ffmpeg-minimal"

# Bump when FFmpeg configure flags change — forces a clean rebuild
FFMPEG_CONFIG_TAG = "novdpau"

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
    The main infinidream binary is compiled with system g++ to preserve ABI
    compatibility with the pre-built libsioclient_tls.a (libstdc++).
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
# Build minimal FFmpeg from source (cached)
# ---------------------------------------------------------------------------
def build_ffmpeg(zig_cc: Path, zig_cxx: Path) -> None:
    """
    Build a minimal FFmpeg (h264/hevc decode, mov/mkv demux, OpenSSL TLS).

    Uses the zig wrappers so bundled .so files are glibc-2.35 clean.
    """
    marker           = FFMPEG_INSTALL / "lib" / "pkgconfig" / "libavcodec.pc"
    toolchain_marker = FFMPEG_INSTALL / ".toolchain-target"
    expected_tag     = f"{GLIBC_TARGET}-{FFMPEG_CONFIG_TAG}"

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
        f"--extra-cflags=-I{OPENSSL_INSTALL}/include",
        f"--extra-ldflags=-L{OPENSSL_INSTALL}/lib",
        "--disable-everything",
        "--enable-shared",
        "--disable-static",
        "--enable-pic",
        "--enable-avformat",
        "--enable-avcodec",
        "--enable-avutil",
        "--enable-swscale",
        "--enable-swresample",
        "--enable-network",
        "--enable-openssl",
        "--enable-protocol=file",
        "--enable-protocol=http",
        "--enable-protocol=https",
        "--enable-protocol=tcp",
        "--enable-protocol=tls",
        "--enable-demuxer=mov",
        "--enable-demuxer=matroska",
        "--enable-decoder=h264",
        "--enable-decoder=hevc",
        "--enable-parser=h264",
        "--enable-parser=hevc",
        "--enable-bsf=h264_mp4toannexb",
        "--enable-bsf=hevc_mp4toannexb",
        "--disable-doc",
        "--disable-programs",
        "--disable-vdpau",
    ], cwd=FFMPEG_SRC_DIR)

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
def build_infinidream(shim_obj: Path) -> None:
    """
    Build infinidream (Release) with system g++.

    System g++ is used (not zig) because zig c++ uses libc++ (LLVM ABI) while
    libsioclient_tls.a was compiled with libstdc++ (GNU ABI).  The shim object
    is injected via CMAKE_EXE_LINKER_FLAGS to intercept versioned symbol bindings.
    """
    nproc = os.cpu_count() or 1

    linker_flags = (
        f"{shim_obj} -lm "
        f"-L{OPENSSL_INSTALL}/lib "
        f"-L{FFMPEG_INSTALL}/lib "
        f"-Wl,--allow-shlib-undefined"
    )

    pkg_config_path = (
        f"{OPENSSL_INSTALL}/lib/pkgconfig:"
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
        "-DBoost_USE_STATIC_LIBS=ON",
        f"-DCMAKE_EXE_LINKER_FLAGS={linker_flags}",
    ], env={"PKG_CONFIG_PATH": pkg_config_path, **os.environ})

    log(f"Compiling ({nproc} jobs) ...")
    run(["cmake", "--build", str(BUILD_DIR), f"-j{nproc}"])


# ---------------------------------------------------------------------------
# glibc ceiling check
# ---------------------------------------------------------------------------
def verify_glibc_ceiling() -> None:
    """
    Fail fast if the binary references glibc symbols newer than 2.35.
    Catches regressions before producing a broken AppImage.
    """
    log("Verifying glibc symbol version ceiling ...")
    binary = BUILD_DIR / "infinidream"
    readelf_out = capture(["readelf", "-sW", str(binary)])

    bad = []
    for line in readelf_out.splitlines():
        # Match GLIBC_2.36 and above (2.36–2.99, 3.x+)
        parts = line.split()
        for part in parts:
            if part.startswith("GLIBC_2."):
                try:
                    minor = int(part.split(".")[1])
                    if minor >= 36:
                        bad.append(part)
                except ValueError:
                    pass

    bad = sorted(set(bad))
    if bad:
        print("\nERROR: Binary references glibc symbols newer than 2.35:", file=sys.stderr)
        for sym in bad:
            print(f"  {sym}", file=sys.stderr)
        print("\nThe AppImage would fail on Ubuntu 22.04 / glibc 2.35.", file=sys.stderr)
        print("Check glibc_compat.c and add wrappers for the above symbols.", file=sys.stderr)
        sys.exit(1)

    log("glibc ceiling check passed — no symbols newer than GLIBC_2.35 found.")


# ---------------------------------------------------------------------------
# Assemble AppDir
# ---------------------------------------------------------------------------
def assemble_appdir() -> None:
    log("Assembling AppDir ...")
    (APPDIR / "usr" / "bin").mkdir(parents=True, exist_ok=True)
    (APPDIR / "usr" / "lib").mkdir(parents=True, exist_ok=True)

    shutil.copy2(BUILD_DIR / "infinidream", APPDIR / "usr" / "bin" / "infinidream")

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
    # glibc — must match host
    "libpthread.so*", "libm.so*", "libc.so*", "libdl.so*", "librt.so*",
    # VDPAU — disabled in FFmpeg build; excluded as belt-and-suspenders
    "libvdpau.so*",
    # Kerberos/GSSAPI — pulled in by libcurl; host libs are glibc-clean
    "libgssapi_krb5.so*", "libkrb5.so*", "libkrb5support.so*",
    "libk5crypto.so*", "libkeyutils.so*",
]


def bundle_libraries(linuxdeploy: Path) -> None:
    log("Running linuxdeploy to bundle shared libraries ...")

    exclude_args: list[str] = []
    for lib in EXCLUDED_LIBS:
        exclude_args += ["--exclude-library", lib]

    ld_library_path = (
        f"{OPENSSL_INSTALL}/lib:{FFMPEG_INSTALL}/lib:"
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
# Create the AppImage
# ---------------------------------------------------------------------------
def create_appimage(appimagetool: Path) -> None:
    log("Creating AppImage ...")
    OUTPUT_APPIMAGE.unlink(missing_ok=True)
    run([str(appimagetool), str(APPDIR), str(OUTPUT_APPIMAGE)])


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main() -> None:
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
        f"{FFMPEG_INSTALL}/lib/pkgconfig:"
        + os.environ.get("PKG_CONFIG_PATH", "")
    )
    os.environ["PKG_CONFIG_PATH"] = pkg_config_path

    build_openssl(zig_cc, zig_cxx)
    build_ffmpeg(zig_cc, zig_cxx)

    linuxdeploy, appimagetool = prepare_appimage_tools()

    shim_obj = compile_glibc_shim()
    build_infinidream(shim_obj)
    verify_glibc_ceiling()

    assemble_appdir()
    bundle_libraries(linuxdeploy)
    create_appimage(appimagetool)

    print()
    log(f"Done! AppImage created at:")
    log(f"  {OUTPUT_APPIMAGE}")
    print()
    log(f"Run with:                   {OUTPUT_APPIMAGE}")
    log(f"Run without FUSE:           APPIMAGE_EXTRACT_AND_RUN=1 {OUTPUT_APPIMAGE}")


if __name__ == "__main__":
    main()
