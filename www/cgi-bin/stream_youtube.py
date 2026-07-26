#!/usr/bin/env python3
import sys
import urllib.request

print("Status: 200 OK")
print("Content-Type: video/mp4")
print("")
sys.stdout.flush()

import os

# Stream a local video file to avoid any internet/bot blocking issues
video_path = "../uploads/test_video.mp4"

try:
    with open(video_path, "rb") as f:
        while True:
            # Read in 8KB chunks
            chunk = f.read(8192)
            if not chunk:
                break

            # Use sys.stdout.buffer to write raw binary data safely
            sys.stdout.buffer.write(chunk)
            sys.stdout.buffer.flush()
except Exception as e:
    # Log the error to stdout so we can see why it failed in curl
    sys.stdout.buffer.write(b"Stream Error: " + str(e).encode())
    sys.stdout.buffer.flush()
    pass
