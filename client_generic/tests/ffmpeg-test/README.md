# FFmpeg Test - Minimal Decoder

Minimal test program that uses FFmpeg directly to decode video files and count frames.

## Prerequisites

- FFmpeg libraries installed (the Makefile expects them in `../../../cache/univ-osx/`)
- C++17 compatible compiler (g++ or clang++)
- macOS with required frameworks (VideoToolbox, CoreFoundation, etc.)
- Relies on videos in `../test-videos`. Can be generated with the .sh script one level down. 

## Building the Test

```bash
make -f Makefile.minimal
```

This will create an executable called `test_decoder_minimal`.

## Running the Test

### Quick Run

```bash
make -f Makefile.minimal run
```

This will run the test on `../test-videos/exact_100_frames.mp4`.

### Manual Run

You can also run the test manually with any video file (use sheep for a sample sheep):

```bash
./test_decoder_minimal ../test-videos/sheep.mp4
```
