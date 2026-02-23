#!/bin/bash
set -e

# 1. Create a 5-second 4K raw video (color bars)
ffmpeg -y -v warning -f lavfi -i smptebars=size=3840x2160:rate=30 -t 5 -pix_fmt yuv420p raw_uhd_video.yuv

# 2. Re-encode dummy to H.265 (UHD HEVC)
ffmpeg -y -v warning -f rawvideo -pix_fmt yuv420p -s 3840x2160 -r 30 -i raw_uhd_video.yuv -c:v libx265 -preset fast -pix_fmt yuv420p tests/sample_uhd_hevc.mp4

# 3. Create an audio track with a tone
ffmpeg -y -v warning -f lavfi -i aevalsrc="sin(440*2*PI*t):s=48000:d=5" -c:a aac -b:a 128k tone_5s.aac

# 4. Combine UHD HEVC video with tone audio
ffmpeg -y -v warning -i tests/sample_uhd_hevc.mp4 -i tone_5s.aac -c:v copy -c:a copy tests/sample_uhd_hevc_with_audio.mp4

# Cleanup temporary files
rm raw_uhd_video.yuv tone_5s.aac

echo "UHD HEVC Test videos created successfully:"
ls -l tests/sample_uhd*.mp4
