Copyright e-dream, inc
======================

2023.07     forked to work with new server and repository, renamed from Electric Sheep to e-dream
2015.05     moved from code.google.com repo
2011.01.30  based on revision 1546 on sf.net

e-dream is a platform for generative visuals.
this repository has the native client.

# dev docs

this repository uses git LFS, be sure to run

    brew install git-lfs
    git lfs install

on Mac, open client_generic/MacBuild/e-dream.xcodeproj

Use the "File>Packages>Update to Latest" menu to load the
dependencies.

The C++ dependencies are handled by vcpkg...

There are four targets: app, screensaver, staging app, and staging
screensaver. The staging targets have their own directory
/Users/Shared/e-dream.ai-stage that can coexist with the normal one
/Users/Shared/e-dream.ai

# to release

make a git tag with the version
```
git tag 0.4.0
git push --tags
```

archive the app and export with automatic notarization. zip the
results and rename. upload to google drive.

the screensaver is more complicated because Xcode can't automatically
manage the signing. So

1) Use Archive in Xcode and export in a folder

2) In that folder, go into the mess of subfolders until you find e-dream.saver. 
OR ALTERNATIVELY (replace {username} with the macOS account name that created the build)

```
mv Products/Users/{username}/Library/Screen\ Savers/e-dream.saver/ ./
```

3) Zip the saver so it can be submitted

```
/usr/bin/ditto -c -k --keepParent "e-dream.saver" "e-dream.zip"
```

4) Assuming  you have created a keychain profile called "e-dream", this will launch the notarization process and wait until it's done

```
xcrun notarytool submit e-dream.zip --keychain-profile "e-dream" --wait
```

5) if successful staple the receipt (this lets people install the screensaver without connecting to apple servers for verification of the notarization)

```
xcrun stapler staple e-dream.saver
```

6) Zip the final thing

```
/usr/bin/ditto -c -k --keepParent "e-dream.saver" "e-dream-master-notarized+stapled.zip"
```

see https://developer.apple.com/documentation/security/customizing-the-notarization-workflow
and https://support.apple.com/en-us/102654

Windows build
=============

The client project requires Visual Studio to build. It is compiled with MSVC (not clang) and compiled/linked against the MSVC Runtime. 

- Install VS Community 2022 v17.10+. As of writing, only available as preview : https://visualstudio.microsoft.com/vs/preview/ (see known setup issues below)

**During installation, make sure "C++ desktop development" and "C++ game develoment" are selected.**

- Integrate vcpkg (cmd from repo base) :

```
cd vcpkg
bootstrap-vcpkg.bat -disableMetrics
.\vcpkg.exe integrate install
``` 

- Open client_generic\MSVC\e-dream.sln in Visual Studio
- Select target DebugMD and build. This will take a *very* long time the first time, as every dependency will get built twice (vcpkg automatically builds from source both debug and release libraries).

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
~~- socket.io is temporarily disabled.~~
- There is no UI for configuration.

- Some includes and manually linked libraries still need cleanup