Copyright e-dream, inc

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

- Install VS Community 2022 v17.10+. As of writing, only available as preview : https://visualstudio.microsoft.com/vs/preview/
- Integrate vcpkg (cmd from repo base) :

```
cd vcpkg && bootstrap-vcpkg.bat
.\vcpkg.exe integrate install
``` 

- Open client_generic\MSVC\e-dream.sln in Visual Studio
- Select target DebugMD and build. This may take a very long time the first time, depending on if the cache (/vcpkg_installed) is commited or not. 

Known issues/Workarounds : 
- Ranges won't compile on MSVC : https://developercommunity.visualstudio.com/t/MSVC-ranges-wont-compile-under-std:c/10532125?sort=newest
-> Install VS Community preview instead
- Libpng requires /MD and crashes on /MDd (debug)f) : https://stackoverflow.com/questions/22774265/libpng-crashes-on-png-read-info
This is why DebugMD exists, it's a "debug" build in /MD mode (instead of /MDd). Will need a workaround for proper DX12 dev, this prevents using debug mode with DirectX (warp renderer + akk associated debug stuff).
