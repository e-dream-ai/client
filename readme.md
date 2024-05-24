Copyright e-dream, inc

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
