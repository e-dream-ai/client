#!/usr/bin/env python3
"""
Windows packaging: build a distributable ZIP and/or NSIS Setup.exe directly
from the MSBuild output directory (client_generic/MSVC/<Configuration>/).

Structure and CLI style align with client_generic/MacBuild/release.py; there
is no Sparkle appcast on Windows.
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


def installer_dir(repo: Path) -> Path:
    return repo / "client_generic" / "InstallerMSVC"


def ensure_dir(p: Path) -> None:
    p.mkdir(parents=True, exist_ok=True)


def check_msvc_output(repo: Path, configuration: str) -> None:
    out = msvc_out_dir(repo, configuration)
    if not out.is_dir():
        print_red(f"Build output directory not found: {out}")
        print_red("Run: python client_generic/WinBuild/build.py")
        sys.exit(1)
    debug = configuration.lower().startswith("debug")
    exe = out / ("infinidreamd.exe" if debug else "infinidream.exe")
    if not exe.is_file():
        print_red(f"Missing executable: {exe}")
        sys.exit(1)
    scr = out / ("infinidreamd.scr" if debug else "infinidream.scr")
    if not scr.is_file():
        print_yellow(f"Missing screensaver binary: {scr} (PostBuild copy may have been skipped)")


DIST_README_NAME = "dist_readme.txt"


def make_distribution_zip(
    repo: Path,
    *,
    version: str,
    configuration: str,
    output_dir: Path,
) -> Path:
    check_msvc_output(repo, configuration)
    source_root = msvc_out_dir(repo, configuration)

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
        readme_src = winbuild_dir(repo) / DIST_README_NAME
        if readme_src.is_file():
            zf.write(readme_src, "README.txt")
        else:
            print_yellow(f"Skipping zip README (missing {readme_src})")

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


def find_makensis() -> Optional[Path]:
    env = os.environ.get("MAKENSIS")
    if env:
        p = Path(env)
        if p.is_file():
            return p
    on_path = shutil.which("makensis")
    if on_path:
        return Path(on_path)
    candidates = [
        os.environ.get("ProgramFiles(x86)"),
        os.environ.get("ProgramFiles"),
        r"C:\Program Files (x86)",
        r"C:\Program Files",
    ]
    for base in candidates:
        if not base:
            continue
        cand = Path(base) / "NSIS" / "makensis.exe"
        if cand.is_file():
            return cand
    return None


def run_makensis(
    repo: Path,
    *,
    version: str,
    configuration: str,
    output_dir: Path,
) -> Path:
    check_msvc_output(repo, configuration)

    nsi_dir = installer_dir(repo)
    nsi = nsi_dir / "nsis_installer.nsi"
    if not nsi.is_file():
        print_red(f"NSIS script not found: {nsi}")
        sys.exit(1)
    makensis_path = find_makensis()
    if not makensis_path:
        print_red(
            "makensis not found. Install NSIS 3.x (`winget install NSIS.NSIS`) "
            "or set MAKENSIS to the full path of makensis.exe."
        )
        sys.exit(1)
    makensis = str(makensis_path)

    ensure_dir(output_dir)
    out_file = output_dir / f"infinidream-windows-{version}-setup.exe"
    source_dir = msvc_out_dir(repo, configuration)

    cmd = [
        makensis,
        f"/DPRODUCT_VERSION={version}",
        f"/DSOURCE_DIR={source_dir}",
        f"/DOUT_FILE={out_file}",
        "nsis_installer.nsi",
    ]
    print_blue(f"Running NSIS: {' '.join(cmd)} (cwd={nsi_dir})")
    run_command(cmd, cwd=nsi_dir)
    if not out_file.is_file():
        print_red(f"Expected output missing: {out_file}")
        sys.exit(1)
    print_green(f"Installer created: {out_file}")
    return out_file


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
            "Windows release helper: build NSIS Setup.exe (default) and/or ZIP, "
            "upload to GitHub."
        ),
    )
    parser.add_argument(
        "--configuration",
        default="Release",
        help="MSBuild configuration to package (default: Release).",
    )
    parser.add_argument(
        "-v",
        "--version",
        dest="version",
        default="0.0.0",
        help="Version string for filenames and the installer (default: 0.0.0).",
    )
    parser.add_argument(
        "--zip",
        action="store_true",
        help="Also build a flat ZIP of the MSVC output.",
    )
    parser.add_argument(
        "--no-installer",
        action="store_true",
        help="Skip NSIS installer (useful with --zip when makensis is unavailable).",
    )
    parser.add_argument(
        "--output-dir",
        default=None,
        help="Directory for artifacts (default: client_generic/WinBuild/dist).",
    )
    parser.add_argument(
        "--sign",
        action="store_true",
        help="Authenticode-sign artifacts (requires SIGN_THUMBPRINT or SIGN_PFX).",
    )
    parser.add_argument(
        "--github-release",
        metavar="TAG",
        default=None,
        help="Upload produced artifacts to GitHub release TAG.",
    )
    args = parser.parse_args()

    repo = repo_root_from_script()
    out_dir = Path(args.output_dir) if args.output_dir else winbuild_dir(repo) / "dist"

    artifacts: list[Path] = []

    if not args.no_installer:
        setup = run_makensis(
            repo,
            version=args.version,
            configuration=args.configuration,
            output_dir=out_dir,
        )
        artifacts.append(setup)

    if args.zip:
        zip_path = make_distribution_zip(
            repo,
            version=args.version,
            configuration=args.configuration,
            output_dir=out_dir,
        )
        artifacts.append(zip_path)

    if not artifacts:
        print_red("Nothing to do: --no-installer was passed without --zip.")
        sys.exit(1)

    if args.sign:
        for a in artifacts:
            sign_file(a)

    if args.github_release:
        for a in artifacts:
            github_upload(a, args.github_release)


if __name__ == "__main__":
    main()
