#!/usr/bin/env python3
"""
infinidream Disk-Full Regression Test (macOS)

Reproduces the launch crash from issue #664 (crash when the disk is full) in a
safe, self-cleaning way: instead of filling the real boot disk, it creates a
tiny RAM disk, points the app's data root at it, fills it so that writes hit a
genuine ENOSPC, launches the app, and reports whether it crashed.

By default it redirects the WHOLE data root (/Users/Shared/infinidream.ai),
which is where settings.json, the json/ metadata, mp4/ content, and Logs/ all
live - so every write path the reporter hit is exercised (including the very
first settings.json write), not just content downloads. The RAM disk starts
empty and full, which faithfully reproduces a clean first-launch on a full
disk. Use --scope content to redirect only the content/ subfolder instead.

Nothing here touches your real disk's free space, and teardown (restore the
original data root, eject the RAM disk) always runs, even on failure or Ctrl-C.

Exit status:
    0  PASS - the app survived a full content disk (the fix works)
    1  FAIL - the app crashed / aborted (issue #664 reproduced)
    2  ERROR - the test could not be set up or run

The --free knob sets how much space the app has at launch, so you can cover the
common scenarios:
    --free 0     totally full: crashes/aborts on the first write at launch
    --free 512K  nearly full: may start, then fail writing settings/json
    --free 30M   starts fine, then runs out while downloading dreams (use a
                 longer --watch so downloads have time to fill it)

Examples:
    ./test_disk_full.py                          # totally full (default --free 0)
    ./test_disk_full.py --free 30M --watch 120   # fill-up-while-downloading case
    ./test_disk_full.py --app /Applications/infinidream.app --free 512K
    ./test_disk_full.py --scope content          # only fill content/, keep root real
"""

import argparse
import os
import shutil
import signal
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Optional


# ANSI color codes (matching build.py / release.py)
class Colors:
    RED = '\033[0;31m'
    GREEN = '\033[0;32m'
    YELLOW = '\033[1;33m'
    BLUE = '\033[0;34m'
    NC = '\033[0m'


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


# Default name for the RAM disk volume (mounts at /Volumes/<NAME>).
RAM_DISK_NAME = "infinidream_diskfull_test"

# Extra capacity added on top of the requested free space. The volume is sized
# to (free + this), then filled back down so EXACTLY `free` bytes remain. The
# slack absorbs filesystem metadata and lets us hit free=0 (a truly full disk).
# Only this slack is ever pre-filled, so RAM use stays small regardless of free.
OVERHEAD_BYTES = 16 * 1024 * 1024  # 16 MB


def parse_size(text: str) -> int:
    """Parse a human size like '0', '512K', '30M', '1G' into bytes (1024-based)."""
    t = text.strip().upper()
    mult = 1
    if t and t[-1] in "KMG":
        mult = {"K": 1024, "M": 1024**2, "G": 1024**3}[t[-1]]
        t = t[:-1]
    try:
        value = float(t)
    except ValueError:
        raise argparse.ArgumentTypeError(f"invalid size: {text!r}")
    if value < 0:
        raise argparse.ArgumentTypeError(f"size must be >= 0: {text!r}")
    return int(value * mult)


def human(num_bytes: float) -> str:
    """Format bytes as a short human string (e.g. '30.0 MB')."""
    size = float(num_bytes)
    for unit in ("B", "KB", "MB", "GB"):
        if abs(size) < 1024 or unit == "GB":
            return f"{size:.0f} {unit}" if unit == "B" else f"{size:.1f} {unit}"
        size /= 1024
    return f"{size:.1f} GB"


def data_root(stage: bool) -> Path:
    """Per-user data root on macOS (see AGENTS.md)."""
    base = "infinidream.ai-stage" if stage else "infinidream.ai"
    return Path("/Users/Shared") / base


def run(cmd, **kwargs) -> subprocess.CompletedProcess:
    """Run a command, returning the CompletedProcess. Never raises on nonzero."""
    return subprocess.run(cmd, capture_output=True, text=True, **kwargs)


def create_ram_disk(size_bytes: int) -> str:
    """Create and mount a RAM disk. Returns its mount point (/Volumes/<NAME>)."""
    sectors = max(1, (size_bytes + 511) // 512)  # round up to 512-byte sectors
    attach = run(["hdiutil", "attach", "-nomount", f"ram://{sectors}"])
    if attach.returncode != 0:
        raise RuntimeError(f"hdiutil attach failed: {attach.stderr.strip()}")
    device = attach.stdout.strip().split()[0]  # e.g. /dev/disk7

    fmt = run(["diskutil", "erasevolume", "HFS+", RAM_DISK_NAME, device])
    if fmt.returncode != 0:
        # Best-effort detach of the raw device before bailing out.
        run(["hdiutil", "detach", device, "-force"])
        raise RuntimeError(f"diskutil erasevolume failed: {fmt.stderr.strip()}")

    mount_point = f"/Volumes/{RAM_DISK_NAME}"
    print_green(f"  Created RAM disk: {device} ({human(size_bytes)}) -> {mount_point}")
    return mount_point


def eject_ram_disk() -> None:
    """Eject the RAM disk if present. Best-effort."""
    mount_point = f"/Volumes/{RAM_DISK_NAME}"
    if not os.path.ismount(mount_point) and not Path(mount_point).exists():
        return
    res = run(["diskutil", "eject", RAM_DISK_NAME])
    if res.returncode == 0:
        print_green(f"  Ejected RAM disk {mount_point}")
    else:
        print_yellow(f"  Could not eject {mount_point}: {res.stderr.strip()}")


def fill_volume(mount_point: str, leave_free_bytes: int) -> None:
    """
    Fill the volume so that ~leave_free_bytes remain free. Only the slack above
    the target is written, so this stays cheap even for a large free budget.
    """
    st = os.statvfs(mount_point)
    free_bytes = st.f_bavail * st.f_frsize
    target = max(0, free_bytes - leave_free_bytes)  # bytes to consume now

    fill_path = Path(mount_point) / "fill.bin"
    chunk = b"\0" * (1024 * 1024)  # 1 MB
    written = 0
    with open(fill_path, "wb") as f:
        try:
            while written + len(chunk) <= target:
                f.write(chunk)
                written += len(chunk)
            # Top up the remainder with a smaller write.
            remainder = target - written
            if remainder > 0:
                f.write(b"\0" * remainder)
                written += remainder
            f.flush()
            os.fsync(f.fileno())
        except OSError:
            # Hit ENOSPC early - that's fine, the disk is full, which is the point.
            pass

    st = os.statvfs(mount_point)
    now_free = st.f_bavail * st.f_frsize
    print_green(f"  Consumed {human(written)}; {human(now_free)} now free "
                f"(target {human(leave_free_bytes)})")


def redirect_dir(target_dir: Path, mount_point: str) -> Optional[Path]:
    """
    Point target_dir at the RAM disk via symlink.

    If target_dir already exists, it is moved aside and the backup path is
    returned so it can be restored during teardown. Returns None if there was
    nothing to back up.
    """
    backup: Optional[Path] = None
    if target_dir.is_symlink() or target_dir.exists():
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        backup = target_dir.with_name(target_dir.name + f".bak.{stamp}")
        shutil.move(str(target_dir), str(backup))
        print_yellow(f"  Moved {target_dir} aside -> {backup}")

    target_dir.parent.mkdir(parents=True, exist_ok=True)
    os.symlink(mount_point, target_dir)
    print_green(f"  Linked {target_dir} -> {mount_point}")
    return backup


def restore_dir(target_dir: Path, backup: Optional[Path]) -> None:
    """Undo redirect_dir."""
    if target_dir.is_symlink():
        target_dir.unlink()
        print_green(f"  Removed symlink {target_dir}")
    if backup is not None and backup.exists():
        # Only restore if the symlink is gone (it should be).
        if not target_dir.exists():
            shutil.move(str(backup), str(target_dir))
            print_green(f"  Restored original from {backup}")
        else:
            print_yellow(f"  Left backup in place: {backup}")


def app_binary(app_path: Path) -> Path:
    """Resolve the executable inside a .app bundle."""
    macos = app_path / "Contents" / "MacOS"
    if not macos.is_dir():
        raise RuntimeError(f"Not an app bundle: {app_path}")
    # Prefer a binary named like the bundle, else take the first executable.
    candidates = [p for p in macos.iterdir() if p.is_file() and os.access(p, os.X_OK)]
    if not candidates:
        raise RuntimeError(f"No executable found in {macos}")
    named = [p for p in candidates if "infinidream" in p.name.lower()]
    return named[0] if named else candidates[0]


def launch_and_watch(binary: Path, watch_secs: int) -> int:
    """
    Launch the app binary and watch it for watch_secs.

    Returns the process exit code if it exits within the window (a crash),
    or None if it is still alive at the end (survived).
    """
    print_blue(f"  Launching {binary.name}, watching for {watch_secs}s ...")
    proc = subprocess.Popen([str(binary)],
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    deadline = time.time() + watch_secs
    while time.time() < deadline:
        code = proc.poll()
        if code is not None:
            return code
        time.sleep(0.5)

    # Still running -> it survived the full disk. Shut it down cleanly.
    proc.terminate()
    try:
        proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        proc.kill()
    return None


def show_recent_log(stage: bool) -> None:
    """Print the tail of today's log, highlighting space-related lines."""
    log_dir = data_root(stage) / "Logs"
    log_file = log_dir / (datetime.now().strftime("%Y_%m_%d") + ".log")
    if not log_file.exists():
        print_yellow(f"  No log file at {log_file}")
        return
    print_blue(f"  Relevant lines from {log_file}:")
    try:
        lines = log_file.read_text(errors="replace").splitlines()
    except OSError as e:
        print_yellow(f"  Could not read log: {e}")
        return
    keywords = ("space", "ENOSPC", "error", "fail", "disk", "full")
    hits = [ln for ln in lines if any(k in ln.lower() for k in keywords)]
    for ln in hits[-25:]:
        print(f"    {ln}")
    if not hits:
        print("    (no space/error lines found; showing last 10 lines)")
        for ln in lines[-10:]:
            print(f"    {ln}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Reproduce the disk-full launch crash (issue #664) safely "
                    "via a RAM disk.")
    parser.add_argument("--app", type=Path,
                        default=Path("/Applications/infinidream.app"),
                        help="Path to the .app bundle to test "
                             "(default: /Applications/infinidream.app)")
    parser.add_argument("--stage", action="store_true",
                        help="Use the staging data root (infinidream.ai-stage)")
    parser.add_argument("--scope", choices=["root", "content"], default="root",
                        help="What to redirect onto the full RAM disk: 'root' "
                             "(whole data root - settings.json, json, mp4, Logs; "
                             "default) or 'content' (only the content/ subdir)")
    parser.add_argument("--target-dir", type=Path, default=None,
                        help="Override the directory to redirect "
                             "(default depends on --scope)")
    parser.add_argument("--free", type=parse_size, default="0",
                        help="Free space the app has at launch, e.g. 0, 512K, "
                             "30M, 1G (default: 0 = totally full). Small "
                             "non-zero values let the app start and then run "
                             "out while downloading dreams.")
    parser.add_argument("--watch", type=int, default=15,
                        help="Seconds to watch for a crash (default: 15). Use a "
                             "longer window with a non-zero --free so downloads "
                             "have time to fill the disk.")
    parser.add_argument("--no-launch", action="store_true",
                        help="Set up the full disk but do not launch the app "
                             "(for manual testing); leaves everything mounted")
    args = parser.parse_args()

    if sys.platform != "darwin":
        print_red("This test only runs on macOS.")
        return 2

    if not args.no_launch and not args.app.exists():
        print_red(f"App bundle not found: {args.app}")
        print_yellow("Build/install it first, or pass --app <path>.")
        return 2

    root = data_root(args.stage)
    if args.target_dir is not None:
        target_dir = args.target_dir
    elif args.scope == "content":
        target_dir = root / "content"
    else:
        target_dir = root

    free_bytes = args.free
    disk_bytes = free_bytes + OVERHEAD_BYTES

    print_blue("=== infinidream disk-full regression test (issue #664) ===")
    print(f"  App:         {args.app}")
    print(f"  Scope:       {args.scope}  ->  {target_dir}")
    print(f"  Free at launch: {human(free_bytes)}"
          f"{'  (totally full)' if free_bytes == 0 else ''}")
    print()

    mount_point: Optional[str] = None
    backup: Optional[Path] = None
    try:
        print_blue("Setup:")
        mount_point = create_ram_disk(disk_bytes)
        backup = redirect_dir(target_dir, mount_point)
        fill_volume(mount_point, free_bytes)
        print()

        if args.no_launch:
            print_yellow("--no-launch: full disk is set up and mounted.")
            print_yellow("  Launch the app manually to observe the behavior.")
            print_yellow("  When done, restore your data with:")
            print_yellow(f"    rm {target_dir}")
            print_yellow(f"    diskutil eject {RAM_DISK_NAME}")
            if backup is not None:
                print_yellow(f"    mv {backup} {target_dir}")
            # Skip teardown so the user can test manually.
            backup = None
            mount_point = None
            return 0

        print_blue("Run:")
        exit_code = launch_and_watch(app_binary(args.app), args.watch)
        print()
        show_recent_log(args.stage)
        print()

        if exit_code is None:
            print_green("PASS: app survived a full disk without crashing.")
            return 0
        else:
            if exit_code < 0:
                sig = signal.Signals(-exit_code).name
                print_red(f"FAIL: app crashed on launch "
                          f"(killed by {sig}) - issue #664 reproduced.")
            else:
                print_red(f"FAIL: app exited with code {exit_code} on a full "
                          f"disk - issue #664 reproduced.")
            return 1

    except KeyboardInterrupt:
        print_yellow("\nInterrupted - cleaning up ...")
        return 2
    except Exception as e:
        print_red(f"ERROR: {e}")
        return 2
    finally:
        if mount_point is not None or backup is not None:
            print()
            print_blue("Teardown:")
            restore_dir(target_dir, backup)
            eject_ram_disk()


if __name__ == "__main__":
    sys.exit(main())
