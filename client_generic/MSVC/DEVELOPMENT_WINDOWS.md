# Windows development and deployment

This document covers the native **MSVC** client ([e-dream.sln](e-dream.sln)), **vcpkg** manifest dependencies, and the **NSIS installer** / ZIP distribution via [client_generic/WinBuild](../WinBuild/) (same style as [MacBuild/build.py](../MacBuild/build.py) and [MacBuild/release.py](../MacBuild/release.py)).

## Prerequisites

- **Visual Studio 2022** (or newer) with **Desktop development with C++** and the **Windows 10/11 SDK**.
- **Python 3.10+** on PATH (for `WinBuild/*.py`).
- **Git LFS** (see repository [README.md](../../README.md)).
- **vcpkg** is vendored as a git submodule at `vcpkg/` (see below).
- **NSIS 3.x** (`winget install NSIS.NSIS`) to build the installer. `release.py` auto-discovers `makensis.exe` in the default NSIS install directory; alternatively set `MAKENSIS=<path>`. Skip with `release.py --no-installer` if you only need the ZIP.

### Toolsets and Win32

The solution mixes toolsets: **x64** configurations use **v143**; **Win32** configurations may still reference **v140** and legacy **Boost** paths inside [electricsheep.vcxproj](electricsheep.vcxproj). Prefer **Release | x64** for current development unless you maintain Win32 explicitly.

## vcpkg (manifest mode)

The repo root [vcpkg.json](../../vcpkg.json) lists dependencies. The project enables **`VcpkgEnableManifest`** in [electricsheep.vcxproj](electricsheep.vcxproj); Visual Studio resolves packages when **`VCPKG_ROOT`** points at your vcpkg clone (the submodule checkout).

After clone:

```bat
git submodule update --init --recursive
vcpkg\bootstrap-vcpkg.bat
```

Install libraries for x64 (typical):

```bat
vcpkg\vcpkg install --triplet x64-windows
```

For **Win32** builds you may need **`x86-windows`**. Set the user or system environment variable **`VCPKG_ROOT`** to the full path of the `vcpkg` directory (e.g. `D:\work\client\vcpkg`), then restart Visual Studio.

You can also run a one-off install from the repo root via:

```bat
python client_generic\WinBuild\build.py --run-vcpkg --triplet x64-windows
```

(Requires bootstrap already run once so `vcpkg.exe` exists.)

## Building

### Visual Studio

Open [e-dream.sln](e-dream.sln), select **Release** and **x64**, then build. The **output directory** is:

`client_generic\MSVC\Release\`

Binaries are named **`infinidream.exe`** and **`infinidream.scr`**.

Place extra runtime **DLLs** (and any other drop-in files) under **`client_generic\MSVC\dll\Release\`** or **`dll\Debug\`**, matching the MSBuild **configuration** you are building (same spelling: `Release`, `Debug`, `DebugMD`, etc.). After each successful link, MSBuild copies all files under that configuration folder into the executable directory (`$(OutDir)`), including nested paths under that folder (**files land flat** in `OutDir`; duplicate basenames overwrite). If **`dll\<Configuration>\`** does not exist, the step is skipped.

### Command line (recommended)

From the repository root:

```bat
python client_generic\WinBuild\build.py
```

Defaults: **Release**, **x64**, restores packages, then builds the solution.

Useful flags:

| Flag | Description |
|------|-------------|
| `-v SEMVER`, `--version SEMVER` | Embed this version in the binary (Settings / API). Omit to take **VER_*** from `Common/clientversion.h`. Also embeds **`git rev-parse --short HEAD`** (or `unknown` without a repo). |
| `-r`, `--release` | Configuration **Release** (default). |
| `-d`, `--debug` | Configuration **Debug** (output `infinidreamd.exe`). |
| `--configuration NAME` | Any MSBuild configuration (e.g. `DebugMD`). |
| `--platform Win32` or `x64` | Default **x64**. |
| `--run-vcpkg` | Run `vcpkg install` for `--triplet` before building. |
| `--triplet TRIPLET` | Default **x64-windows**. |
| `--no-restore` | Skip `/restore` on MSBuild. |
| `--msbuild PATH` | Force MSBuild executable (overrides discovery). |

**MSBuild discovery:** uses **`MSBUILD`** env var if set; otherwise **vswhere** (`%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe`) to locate `MSBuild.exe`. You can also add the MSBuild **`Bin`** folder to **`PATH`** so `msbuild.exe` resolves without vswhere.

## Release: installer (default) and ZIP

Build with the same semver you will ship (writes `MSVC/edream_build_version_generated.h` then compiles):

```bat
python client_generic\WinBuild\build.py -v 0.14.0
```

Then package:

```bat
python client_generic\WinBuild\release.py -v 0.14.0              :: NSIS Setup.exe
python client_generic\WinBuild\release.py -v 0.14.0 --zip        :: adds a portable ZIP
python client_generic\WinBuild\release.py -v 0.14.0 --no-installer --zip   :: ZIP only
```

The installer packages the entire **`client_generic\MSVC\Release\`** directory (binaries, vcpkg-linked DLLs, fonts, OSD assets) directly — there is no separate staging step. License/readme pages come from **`client_generic\RuntimeMSVC\License.rtf`** and **`Instructions.rtf`**.

Output:

- **`client_generic\WinBuild\dist\infinidream-windows-{version}-setup.exe`** (NSIS)
- **`client_generic\WinBuild\dist\infinidream-windows-{version}.zip`** (with `--zip`)

Options:

| Flag | Description |
|------|-------------|
| `-v` / `--version` | Version string baked into the installer and file names (default `0.0.0`). |
| `--configuration` | MSVC folder to package (default **Release**). |
| `--zip` | Also build the portable ZIP. |
| `--no-installer` | Skip NSIS; useful when `makensis` is unavailable. |
| `--output-dir DIR` | Where to write artifacts. |
| `--sign` | Authenticode-sign artifacts via `SIGN_THUMBPRINT` or `SIGN_PFX`. |
| `--github-release TAG` | Upload with `gh`. |

### Installer behavior

- **x64 Windows 10+** only; the installer aborts otherwise.
- Installs everything to **`%ProgramFiles%\Infinidream\`** (admin required).
- Creates Start Menu shortcuts (launch, windowed launch, website, uninstall).
- On the finish page, user can opt-in to set Infinidream as the **current screensaver** (writes `HKCU\Control Panel\Desktop\SCRNSAVE.EXE` to the full path of `infinidream.scr` in the install directory).
- Uninstaller prompts to preserve or delete **`%ProgramData%\e-dream\`** (downloaded content and logs).

The NSIS script lives at [../InstallerMSVC/nsis_installer.nsi](../InstallerMSVC/nsis_installer.nsi).

## Testing and symbols

Manual test scenarios mirror macOS (playlists, sign-in, screensaver install). Data locations on Windows differ (e.g. under **`%ProgramData%`** / **`%LOCALAPPDATA%`** per app configuration).

**BugSnag / crash symbols:** macOS uses **dSYM**; Windows builds produce **PDB** files next to binaries (e.g. under `MSVC\Release\`). Upload PDBs using BugSnag’s Windows / symbol upload guidance.

## See also

- [README.md](../../README.md) — platform overview and vcpkg bootstrap.
- [MacBuild/build.py](../MacBuild/build.py), [MacBuild/release.py](../MacBuild/release.py) — macOS build and Sparkle release pipeline.
