Copyright e-dream, inc

2023.07     forked to work with new server and repository, renamed from Electric Sheep to e-dream
2015.05     moved from code.google.com repo
2011.01.30  based on revision 1546 on sf.net

e-dream is a platform for generative visuals.
this repository has the native client.

dev docs
========

install dependencies:

    brew install git-lfs
    git lfs install


dependencies for package management:

    brew install nasm
    git submodule update --init --recursive
    ./vcpkg/bootstrap-vcpkg.sh
    ./build_mac_libs_vcpkg.sh

on Mac, open client_generic/MacBuild/e-dream.xcodeproj

to make a release: archive the app, archive the screensaver, upload as zip files.

there are two configurations, one that connects to the staging server,
and another to the alpha production server. the apps have different
names and storage, so they can coexist.
