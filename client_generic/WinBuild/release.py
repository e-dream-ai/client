#!/usr/bin/env python3
"""
Windows packaging: stage outputs, build a distributable ZIP (default), optional legacy NSIS installer.

Structure and CLI style align with client_generic/MacBuild/release.py; there is no Sparkle appcast.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path
from typing import Optional


class Colors:
    RED = "\033[0;31m"
    GREEN = "\033[0;32m"
    YELLOW = "\033[1;33m"
    BLUE = "\033[0;34m"
    NC = "\033[0m"


def print_color(color: str, message: str) -> None:
    print(f"{color}{message}{Colors.NC}")


def print_red(message: str) -> None:
    print_color(Colors.RED, message)


def print_green(message: str) -> None:
    print_color(Colors.GREEN, message)


def print_yellow(message: str) -> None:
    print_color(Colors.YELLOW, message)


def print_blue(message: str) -> None:
    print_color(Colors.BLUE, message)


def run_command(
    cmd: list[str],
    *,
    check: bool = True,
    capture_output: bool = False,
    env: Optional[dict[str, str]] = None,
    cwd: Optional[str | Path] = None,
) -> subprocess.CompletedProcess[str]:
    merged: dict[str, str] = dict(os.environ)
    if env:
        merged.update(env)
    try:
        return subprocess.run(
            cmd,
            check=check,
            capture_output=capture_output,
            text=True,
            env=merged,
            cwd=cwd,
        )
    except FileNotFoundError:
        print_red(f"Command not found: {cmd[0]}")
        raise


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parent.parent.parent


def winbuild_dir(repo: Path) -> Path:
    return repo / "client_generic" / "WinBuild"


def msvc_out_dir(repo: Path, configuration: str) -> Path:
    return repo / "client_generic" / "MSVC" / configuration


def runtime_msvc_dir(repo: Path) -> Path:
    return repo / "client_generic" / "RuntimeMSVC"


def installer_dir(repo: Path) -> Path:
    return repo / "client_generic" / "InstallerMSVC"


def ensure_dir(p: Path) -> None:
    p.mkdir(parents=True, exist_ok=True)


def stage_build_to_runtime_msvc(
    repo: Path,
    *,
    configuration: str,
    platform: str,
) -> None:
    if platform.lower() != "x64":
        print_yellow("Staging is only verified for x64; continuing anyway.")
    out = msvc_out_dir(repo, configuration)
    if not out.is_dir():
        print_red(f"Build output directory not found: {out}")
        print_red("Run WinBuild/build.py for this configuration first.")
        sys.exit(1)

    debug = configuration.lower().startswith("debug")
    exe_name = "e-dreamd.exe" if debug else "e-dream.exe"
    scr_name = "e-dreamd.scr" if debug else "e-dream.scr"
    exe_src = out / exe_name
    scr_src = out / scr_name
    if not exe_src.is_file():
        print_red(f"Missing {exe_src}")
        sys.exit(1)
    if not scr_src.is_file():
        print_yellow(f"Missing {scr_src} (PostBuild copy); using .exe copy for .scr name")
        scr_src = exe_src

    dest = runtime_msvc_dir(repo)
    ensure_dir(dest)
    shutil.copy2(exe_src, dest / "es.exe")
    shutil.copy2(scr_src, dest / "es.scr")
    print_green(f"Staged es.exe / es.scr from {exe_src.name}")

    for name in (
        "exchndl.dll",
        "pthreadGC2.dll",
        "flam3-animate.exe",
        "Instructions.rtf",
        "License.rtf",
        "logo.png",
    ):
        src = out / name
        if src.is_file():
            shutil.copy2(src, dest / name)

    for png in out.glob("*.png"):
        shutil.copy2(png, dest / png.name)

    scripts_src = out / "scripts"
    scripts_dst = dest / "Scripts"
    if scripts_src.is_dir():
        if scripts_dst.is_dir():
            shutil.rmtree(scripts_dst)
        shutil.copytree(scripts_src, scripts_dst)
        print_green(f"Copied scripts to {scripts_dst}")

    print_green(f"Staging complete under {dest}")


# Paths relative to client_generic/RuntimeMSVC required by nsis_installer.nsi (subset).
INSTALLER_CRITICAL = (
    "es.exe",
    "es.scr",
    "exchndl.dll",
    "pthreadGC2.dll",
    "avcodec-57.dll",
    "avformat-57.dll",
    "avutil-55.dll",
    "swresample-2.dll",
    "swscale-4.dll",
    "setacl.exe",
    "flam3-animate.exe",
    "Instructions.rtf",
    "License.rtf",
    "logo.png",
    "Scripts/class.lua",
)


def check_runtime_msvc(repo: Path, *, strict: bool = True) -> list[str]:
    base = runtime_msvc_dir(repo)
    missing: list[str] = []
    for rel in INSTALLER_CRITICAL:
        if not (base / Path(rel)).is_file():
            missing.append(rel)
    dx_files = (
        "Jun2010_D3DCompiler_43_x86.cab",
        "DXSETUP.exe",
    )
    for rel in dx_files:
        if not (base / rel).is_file():
            missing.append(rel)
    if missing and strict:
        print_red("RuntimeMSVC is missing files that the NSIS script expects:")
        for m in missing:
            print_red(f"  - {m}")
        print_yellow("Stage a build with: python client_generic/WinBuild/release.py --stage")
    return missing


def check_msvc_output(repo: Path, configuration: str) -> None:
    out = msvc_out_dir(repo, configuration)
    if not out.is_dir():
        print_red(f"Build output directory not found: {out}")
        print_red("Run: python client_generic/WinBuild/build.py")
        sys.exit(1)
    debug = configuration.lower().startswith("debug")
    exe = out / ("e-dreamd.exe" if debug else "e-dream.exe")
    if not exe.is_file():
        print_red(f"Missing executable: {exe}")
        sys.exit(1)


def make_distribution_zip(
    repo: Path,
    *,
    version: str,
    configuration: str,
    zip_from: str,
    output_dir: Path,
) -> Path:
    if zip_from == "msvc":
        source_root = msvc_out_dir(repo, configuration)
        check_msvc_output(repo, configuration)
    else:
        source_root = runtime_msvc_dir(repo)
        if not source_root.is_dir():
            print_red(f"RuntimeMSVC not found: {source_root}")
            sys.exit(1)
        if not (source_root / "es.exe").is_file() and not (source_root / "e-dream.exe").is_file():
            print_red("RuntimeMSVC has no es.exe or e-dream.exe. Run with --stage first.")
            sys.exit(1)

    ensure_dir(output_dir)
    inner = f"infinidream-windows-{version}"
    zip_path = output_dir / f"{inner}.zip"
    if zip_path.is_file():
        zip_path.unlink()

    print_blue(f"Creating {zip_path} from {source_root}")

    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for path in source_root.rglob("*"):
            if path.is_file():
                arcname = Path(inner) / path.relative_to(source_root)
                zf.write(path, arcname.as_posix())

    print_green(f"ZIP created: {zip_path}")
    return zip_path


def find_signtool() -> Optional[Path]:
    env = os.environ.get("SIGNTOOL")
    if env:
        p = Path(env)
        if p.is_file():
            return p
    kit = os.environ.get("WindowsKitDir") or os.environ.get("WindowsSdkDir")
    if kit:
        root = Path(kit)
        for cand in root.rglob("signtool.exe"):
            if cand.is_file():
                return cand
    prog_files = os.environ.get("ProgramFiles(x86)") or "C:\\Program Files (x86)"
    kits = Path(prog_files) / "Windows Kits" / "10" / "bin"
    if kits.is_dir():
        versions = sorted([p for p in kits.iterdir() if p.is_dir()], reverse=True)
        for ver in versions:
            for arch in ("x64", "x86", "arm64"):
                cand = ver / arch / "signtool.exe"
                if cand.is_file():
                    return cand
    return None


def sign_file(path: Path) -> None:
    signtool = find_signtool()
    if not signtool:
        print_red("signtool.exe not found. Set SIGNTOOL or install Windows SDK.")
        sys.exit(1)
    thumb = os.environ.get("SIGN_THUMBPRINT")
    pfx = os.environ.get("SIGN_PFX")
    pfx_pw = os.environ.get("SIGN_PFX_PASSWORD", "")
    ts = os.environ.get("SIGN_TIMESTAMP_URL", "http://timestamp.digicert.com")

    cmd: list[str] = [str(signtool), "sign", "/fd", "SHA256", "/tr", ts, "/td", "SHA256"]
    if thumb:
        cmd.extend(["/sha1", thumb])
    elif pfx:
        cmd.extend(["/f", pfx])
        if pfx_pw:
            cmd.extend(["/p", pfx_pw])
    else:
        print_red("Signing requested but neither SIGN_THUMBPRINT nor SIGN_PFX is set.")
        sys.exit(1)
    cmd.append(str(path))
    print_blue(f"Signing: {path}")
    run_command(cmd)
    print_green("Signing finished.")


def run_makensis(
    repo: Path,
    *,
    version: Optional[str],
) -> Path:
    nsi_dir = installer_dir(repo)
    nsi = nsi_dir / "nsis_installer.nsi"
    if not nsi.is_file():
        print_red(f"NSIS script not found: {nsi}")
        sys.exit(1)
    makensis = shutil.which("makensis")
    if not makensis:
        print_red("makensis not found on PATH. Install NSIS 3.x.")
        sys.exit(1)

    cmd = [makensis]
    if version:
        cmd.append(f"/DPRODUCT_VERSION={version}")
    cmd.append("nsis_installer.nsi")
    print_blue(f"Running NSIS: {' '.join(cmd)} (cwd={nsi_dir})")
    run_command(cmd, cwd=nsi_dir)
    setup = nsi_dir / "Setup.exe"
    if not setup.is_file():
        print_red(f"Expected output missing: {setup}")
        sys.exit(1)
    print_green(f"Installer created: {setup}")
    return setup


def github_upload(asset: Path, tag: str) -> None:
    gh = shutil.which("gh")
    if not gh:
        print_red("gh CLI not found on PATH.")
        sys.exit(1)
    if not os.environ.get("GITHUB_TOKEN"):
        print_yellow("GITHUB_TOKEN is not set; gh may still use existing auth.")
    print_blue(f"Uploading {asset.name} to GitHub release {tag}")
    run_command(
        [gh, "release", "upload", tag, str(asset), "--clobber"],
    )
    print_green("Upload finished.")


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Windows release helper: ZIP distribution (default), optional --stage and legacy --installer (NSIS)."
        ),
    )
    parser.add_argument(
        "--stage",
        action="store_true",
        help="Copy e-dream*.exe/.scr and assets from MSVC output into RuntimeMSVC as es.exe / es.scr.",
    )
    parser.add_argument(
        "--configuration",
        default="Release",
        help="MSBuild configuration for --stage / ZIP from msvc (default: Release).",
    )
    parser.add_argument(
        "--platform",
        default="x64",
        help="Platform hint for --stage (default: x64).",
    )
    parser.add_argument(
        "--installer",
        action="store_true",
        help="Build NSIS Setup.exe instead of a ZIP (legacy; requires makensis and full RuntimeMSVC).",
    )
    parser.add_argument(
        "--skip-check",
        action="store_true",
        help="With --installer: do not verify RuntimeMSVC before makensis.",
    )
    parser.add_argument(
        "-v",
        "--version",
        dest="version",
        default="0.0.0",
        help="Version string for the ZIP name (default: 0.0.0), e.g. 0.14.0.",
    )
    parser.add_argument(
        "--zip-from",
        choices=("msvc", "runtime"),
        default="msvc",
        help="ZIP contents from MSVC output (default) or staged RuntimeMSVC.",
    )
    parser.add_argument(
        "--output-dir",
        default=None,
        help="Directory for the .zip (default: client_generic/WinBuild/dist).",
    )
    parser.add_argument(
        "--sign",
        action="store_true",
        help="Sign the ZIP or Setup.exe (requires SIGN_THUMBPRINT or SIGN_PFX).",
    )
    parser.add_argument(
        "--github-release",
        metavar="TAG",
        default=None,
        help="Upload the produced ZIP (or Setup.exe with --installer) to GitHub release TAG.",
    )
    args = parser.parse_args()

    repo = repo_root_from_script()

    if args.stage:
        stage_build_to_runtime_msvc(
            repo,
            configuration=args.configuration,
            platform=args.platform,
        )
        return

    out_dir = Path(args.output_dir) if args.output_dir else winbuild_dir(repo) / "dist"

    if args.installer:
        if not args.skip_check:
            missing = check_runtime_msvc(repo, strict=True)
            if missing:
                sys.exit(1)
        setup = run_makensis(repo, version=args.version if args.version != "0.0.0" else None)
        if args.sign:
            sign_file(setup)
        if args.github_release:
            github_upload(setup, args.github_release)
        return

    zip_path = make_distribution_zip(
        repo,
        version=args.version,
        configuration=args.configuration,
        zip_from=args.zip_from,
        output_dir=out_dir,
    )
    if args.sign:
        sign_file(zip_path)
    if args.github_release:
        github_upload(zip_path, args.github_release)


if __name__ == "__main__":
    main()
