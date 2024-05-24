Copyright e-dream, inc
======================

2023.07     forked to work with new server and repository, renamed from Electric Sheep to e-dream
2015.05     moved from code.google.com repo
2011.01.30  based on revision 1546 on sf.net

e-dream is a platform for generative visuals.
this repository has the native client.

dev docs
========

this repository uses git LFS, be sure to run

    brew install git-lfs
    git lfs install

on Mac, open client_generic/MacBuild/e-dream.xcodeproj

to make a release: archive the app, archive the screensaver, upload as zip files.

to test with staging server, visit
https://creative-rugelach-b78ab4.netlify.app/ and create an account,
and then in the advanced settings change the backend server to:

    e-dream-76c98b08cc5d.herokuapp.com

and then enter your credentials.

Windows build
=============

The client project requires Visual Studio to build. It is compiled with MSVC (not clang) and compiled/linked against the MSVC Runtime. 

Assuming a Windows 10 (or 11) x86-64 machine, this won't work on Windows 7.

- Install VS Community 2022 v17.10+. As of writing, only available as preview : https://visualstudio.microsoft.com/vs/preview/ (see known setup issues below)

**During installation, make sure "C++ desktop development" and "C++ game develoment" are selected.**

- Integrate vcpkg (cmd from repo base) :

```
cd vcpkg
bootstrap-vcpkg.bat -disableMetrics
.\vcpkg.exe integrate install
``` 

- Open client_generic\MSVC\e-dream.sln in Visual Studio
- Select target DebugMD and build. This will take a *very* long time the first time, as every dependency will get built twice (vcpkg automatically builds from source both debug *and* release libraries).

- **DO NOT INSTALL DirectX9 SDK**. DirectX 12 SDK is now bundled with the Windows 10 SDK and will be used automatically.

First launches, setup
===

- Once built, launch once, wait a few seconds and quit. It will be unable  to login as there's currently no UI to input your credentials.
- Make sure your mac is connected to the server you'll want on PC (default is prod)
- Go to `C:\ProgramData\e-dream` and open `e-dream.json`
- Grab the `e-dream.json` from your mac
- Paste those lines that contain your token credentials *from* the mac json *to* your windows json: 

```
            "access_token" : "xxxxxxxxxxxxxxxxxxxxx",
            "refresh_token" : "xxxxxxxxxxxxxxxxxxxxx",
```
- If your mac was **NOT** setup for prod, also change the server line so they match, e.g. : 

```
            "server" : "e-dream-xxxxxxx.herokuapp.com",
```  

Known setup issues/Workarounds : 
===

- Ranges won't compile on MSVC : https://developercommunity.visualstudio.com/t/MSVC-ranges-wont-compile-under-std:c/10532125?sort=newest
-> Install VS Community **preview** instead
- Libpng requires /MD and crashes on /MDd (debug) : https://stackoverflow.com/questions/22774265/libpng-crashes-on-png-read-info
This is why DebugMD exists, it's a "debug" build in /MD mode (instead of /MDd). Will need a workaround for proper DX12 dev, this prevents using debug mode with DirectX (warp renderer + all associated debug stuff).

TODO/known code issues : 
===

- Renderer is partially implemented using DirectX 12.
- Video hardware decoding + passthrough is not implemented (currently uses ffmpeg + copy to texture which is slow).
- Text rendering is not implemented. 
- Shaders are not ported/implemented.
- ~~socket.io is temporarily disabled.~~
- There is no UI for configuration.

- Some includes and manually linked libraries still need cleanup

- ~~Build folders can be gitignored~~

- DirectX headers are still manually included, the vcpkg version may not be up to date, and produce issues while building. Using the manual ones in the meantime.