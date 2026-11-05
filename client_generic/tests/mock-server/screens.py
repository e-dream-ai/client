"""Watches the client's window by taking screenshots, to see what actually reached the screen.

At about one screenshot a second this catches a black or frozen picture. It can't see
individual dropped frames. Mac only for now, and needs Screen Recording permission.
"""
import subprocess
import threading
import time
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageChops, ImageStat

BLACK_BRIGHTNESS = 8  # Mean brightness, 0-255, below which a picture counts as black.
# Mean per-pixel difference, 0-255, below which two pictures match. A paused client measured
# 0.00 and a slow-moving dream about 0.5.
CHANGE_THRESHOLD = 0.2
THUMBNAIL_SIZE = (64, 64)


@dataclass
class Sample:
    t: float
    brightness: float
    thumbnail: Image.Image


def is_black(sample):
    return sample.brightness < BLACK_BRIGHTNESS


def difference(a, b):
    return ImageStat.Stat(ImageChops.difference(a.thumbnail, b.thumbnail)).mean[0]


def longest_black_run(samples):
    """The longest time, in seconds, that consecutive samples were all black."""
    longest = 0.0
    start = None
    for sample in samples:
        if is_black(sample):
            start = start if start is not None else sample.t
            longest = max(longest, sample.t - start)
        else:
            start = None
    return longest


def longest_frozen_run(samples):
    """The longest time, in seconds, that consecutive samples showed the same picture."""
    longest = 0.0
    start = None
    for previous, sample in zip(samples, samples[1:]):
        if difference(previous, sample) < CHANGE_THRESHOLD:
            start = start if start is not None else previous.t
            longest = max(longest, sample.t - start)
        else:
            start = None
    return longest


def can_capture():
    import Quartz
    return Quartz.CGPreflightScreenCaptureAccess()


def find_window(pid):
    """Returns the id of the process's largest on-screen window, or None."""
    import Quartz
    windows = Quartz.CGWindowListCopyWindowInfo(
        Quartz.kCGWindowListOptionOnScreenOnly | Quartz.kCGWindowListExcludeDesktopElements,
        Quartz.kCGNullWindowID) or []
    best = None
    best_area = 0
    for window in windows:
        if window.get(Quartz.kCGWindowOwnerPID) != pid:
            continue
        bounds = window[Quartz.kCGWindowBounds]
        area = bounds["Width"] * bounds["Height"]
        if area > best_area:
            best, best_area = window[Quartz.kCGWindowNumber], area
    return best


class ScreenWatcher:
    """Takes a screenshot of a process's window about once a second, on its own thread."""

    def __init__(self, pid, folder, interval=1.0):
        self.pid = pid
        self.folder = Path(folder)
        self.interval = interval
        self.samples = []
        self.failures = 0
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)

    def start(self):
        self.folder.mkdir(parents=True, exist_ok=True)
        self._thread.start()

    def stop(self):
        self._stop.set()
        self._thread.join()

    def between(self, start, end):
        return [s for s in self.samples if start <= s.t <= end]

    def _capture(self):
        window = find_window(self.pid)
        if window is None:
            return False
        now = time.time()
        path = self.folder / f"{now:.3f}.jpg"
        result = subprocess.run(["screencapture", "-x", "-o", "-t", "jpg", "-l", str(window), str(path)],
                                capture_output=True)
        if result.returncode != 0 or not path.exists():
            return False
        with Image.open(path) as image:
            gray = image.convert("L")
            brightness = ImageStat.Stat(gray).mean[0]
            thumbnail = gray.resize(THUMBNAIL_SIZE)
            # Keep a small copy to look at later, in place of the full-size screenshot.
            preview = image.convert("RGB")
            preview.thumbnail((480, 480))
            preview.save(path, quality=70)
        self.samples.append(Sample(now, brightness, thumbnail))
        return True

    def _run(self):
        while not self._stop.is_set():
            started = time.time()
            # A window can be missing during startup, so this only counts failures.
            if not self._capture():
                self.failures += 1
            self._stop.wait(max(0.0, self.interval - (time.time() - started)))
