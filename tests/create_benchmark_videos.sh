#!/bin/bash
set -e

# Generate Audio Tone
ffmpeg -y -v warning -f lavfi -i aevalsrc="sin(440*2*PI*t):s=48000:d=5" -c:a aac -b:a 128k tone_5s.aac

# 1. HD H264 Benchmark Files (1920x1080)
ffmpeg -y -v warning -f lavfi -i smptebars=size=1920x1080:rate=30 -t 5 -pix_fmt yuv420p raw_hd_video.yuv

bitrates_hd=("5M" "20M" "50M")
for br in "${bitrates_hd[@]}"; do
    echo "Encoding HD H264 at $br..."
    ffmpeg -y -v warning -f rawvideo -pix_fmt yuv420p -s 1920x1080 -r 30 -i raw_hd_video.yuv -c:v libx264 -b:v $br -maxrate $br -bufsize ${br} -pix_fmt yuv420p hd_h264_${br}.mp4
    ffmpeg -y -v warning -i hd_h264_${br}.mp4 -i tone_5s.aac -c:v copy -c:a copy tests/bench_hd_h264_${br}.mp4
    rm hd_h264_${br}.mp4
done

# 2. UHD HEVC Benchmark Files (3840x2160)
ffmpeg -y -v warning -f lavfi -i smptebars=size=3840x2160:rate=30 -t 5 -pix_fmt yuv420p raw_uhd_video.yuv

bitrates_uhd=("10M" "30M" "60M" "100M")
for br in "${bitrates_uhd[@]}"; do
    echo "Encoding UHD HEVC at $br..."
    ffmpeg -y -v warning -f rawvideo -pix_fmt yuv420p -s 3840x2160 -r 30 -i raw_uhd_video.yuv -c:v libx265 -b:v $br -maxrate $br -bufsize ${br} -pix_fmt yuv420p uhd_hevc_${br}.mp4
    ffmpeg -y -v warning -i uhd_hevc_${br}.mp4 -i tone_5s.aac -c:v copy -c:a copy tests/bench_uhd_hevc_${br}.mp4
    rm uhd_hevc_${br}.mp4
done

# Cleanup
rm raw_hd_video.yuv raw_uhd_video.yuv tone_5s.aac

echo "Benchmark videos created successfully."
ls -lh tests/bench_*.mp4
