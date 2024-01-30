Copyright e-dream, inc

2023.07     forked to work with new server and repository, renamed from Electric Sheep to e-dream
2015.05     moved from code.google.com repo
2011.01.30  based on revision 1546 on sf.net

e-dream is a platform for generative visuals.
this repository has the native client.

dev docs
========

on Mac, open client_generic/MacBuild/e-dream.xcodeproj

to make a release, build the screensaver, then build the app, then run the script like this:

% cd client_generic/MacBuild
% ./build_installer.sh 0.1.0 e-dream-0.1.0.dmg

on GitHub, make a branch and PR for each change.
Test then squash-merge each PR.
