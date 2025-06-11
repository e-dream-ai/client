#!/bin/bash

# Test video generator script for ContentDecoder frame extraction tests
# This script creates test videos with known frame counts to verify decoder accuracy

# Create test-videos directory if it doesn't exist
mkdir -p test-videos

echo "Generating test videos for ContentDecoder frame extraction tests..."

# Function to generate a test video with exact frame count
generate_test_video() {
    local output_file=$1
    local frame_count=$2
    local fps=$3
    local resolution=$4
    local codec=$5
    
    # Calculate duration needed for exact frame count
    local duration=$(echo "scale=6; $frame_count / $fps" | bc)
    
    echo "Generating $output_file: $frame_count frames @ ${fps}fps (${duration}s)"
    
    # Generate video with frame numbers overlayed
    ffmpeg -y -f lavfi -i "testsrc2=size=${resolution}:rate=${fps}:duration=${duration}" \
           -vf "drawtext=text='Frame %{n}':x=10:y=10:fontsize=24:fontcolor=white:box=1:boxcolor=black@0.5,\
                drawtext=text='Total ${frame_count}':x=10:y=50:fontsize=20:fontcolor=yellow:box=1:boxcolor=black@0.5" \
           -c:v $codec \
           -pix_fmt yuv420p \
           -frames:v $frame_count \
           "$output_file" 2>/dev/null
    
    if [ $? -eq 0 ]; then
        # Verify frame count
        actual_frames=$(ffprobe -v error -select_streams v:0 -count_packets \
                        -show_entries stream=nb_read_packets -of csv=p=0 "$output_file")
        echo "  Created: $actual_frames frames (expected: $frame_count)"
        if [ "$actual_frames" != "$frame_count" ]; then
            echo "  WARNING: Frame count mismatch!"
        fi
    else
        echo "  ERROR: Failed to generate $output_file"
    fi
}

# Generate test videos with exact frame counts
generate_test_video "test-videos/exact_100_frames.mp4" 100 30 "320x240" "libx264"
generate_test_video "test-videos/exact_250_frames.mp4" 250 30 "320x240" "libx264"
generate_test_video "test-videos/exact_500_frames.mp4" 500 30 "320x240" "libx264"
generate_test_video "test-videos/one_second_30fps.mp4" 30 30 "320x240" "libx264"
generate_test_video "test-videos/ten_seconds_30fps.mp4" 300 30 "320x240" "libx264"

# Generate test video for end frame issue (with distinctive last frames)
echo "Generating end_frame_test.mp4 with color-coded last frames..."
ffmpeg -y -f lavfi -i "testsrc2=size=320x240:rate=30:duration=3.333333" \
       -f lavfi -i "color=red:size=320x240:rate=30:duration=0.066667" \
       -f lavfi -i "color=green:size=320x240:rate=30:duration=0.033333" \
       -filter_complex "[0:v][1:v][2:v]concat=n=3:v=1[out]; \
                        [out]drawtext=text='Frame %{n}':x=10:y=10:fontsize=24:fontcolor=white:box=1:boxcolor=black@0.5[final]" \
       -map "[final]" \
       -c:v libx264 \
       -pix_fmt yuv420p \
       -frames:v 102 \
       "test-videos/end_frame_test.mp4" 2>/dev/null

# Generate videos with different codecs
echo "Generating codec test videos..."
generate_test_video "test-videos/h264_test.mp4" 150 25 "640x480" "libx264"
generate_test_video "test-videos/h265_test.mp4" 150 25 "640x480" "libx265"

# Generate video with B-frames
echo "Generating B-frame test video..."
ffmpeg -y -f lavfi -i "testsrc2=size=640x480:rate=30:duration=5" \
       -vf "drawtext=text='Frame %{n}':x=10:y=10:fontsize=24:fontcolor=white" \
       -c:v libx264 \
       -x264-params "bframes=3:b-adapt=2" \
       -pix_fmt yuv420p \
       -frames:v 150 \
       "test-videos/bframes_test.mp4" 2>/dev/null

# Generate very short video (edge case)
generate_test_video "test-videos/short_5_frames.mp4" 5 30 "320x240" "libx264"

# Generate video with non-standard frame rate
generate_test_video "test-videos/fps_23976.mp4" 120 23.976 "320x240" "libx264"

# Create a summary file
echo "Creating test video summary..."
cat > test-videos/test_videos.txt << EOF
Test Videos Generated:
======================
exact_100_frames.mp4  - Exactly 100 frames @ 30fps
exact_250_frames.mp4  - Exactly 250 frames @ 30fps
exact_500_frames.mp4  - Exactly 500 frames @ 30fps
one_second_30fps.mp4  - 30 frames (1 second @ 30fps)
ten_seconds_30fps.mp4 - 300 frames (10 seconds @ 30fps)
end_frame_test.mp4    - 102 frames with colored last frames (red & green)
h264_test.mp4         - 150 frames with H.264 codec
h265_test.mp4         - 150 frames with H.265 codec
bframes_test.mp4      - 150 frames with B-frames
short_5_frames.mp4    - Edge case: only 5 frames
fps_23976.mp4         - 120 frames @ 23.976fps

To verify frame counts manually:
ffprobe -v error -select_streams v:0 -count_packets -show_entries stream=nb_read_packets -of csv=p=0 <video_file>
EOF

echo "Test video generation complete!"
echo "Summary saved to test-videos/test_videos.txt"

# Make script executable
chmod +x generate_test_videos.sh