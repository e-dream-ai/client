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
