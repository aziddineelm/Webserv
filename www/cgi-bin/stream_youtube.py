#!/usr/bin/env python3
import sys
import subprocess
import os

print("Status: 200 OK")
print("Content-Type: video/mp4")
print("")
sys.stdout.flush()

youtube_url = "https://www.youtube.com/watch?v=1mBc8VjDa1s"
yt_dlp_path = os.path.expanduser("~/.local/bin/yt-dlp")

# Stream best audio+video combined format (format 18 is 360p mp4)
command = [
    yt_dlp_path,
    "-f", "18",
    "-o", "-",
    "--quiet",
    youtube_url
]

try:
    # Pass fd 1 directly so yt-dlp streams raw binary video data straight to Webserv
    subprocess.run(command, stdout=1)
except Exception as e:
    pass
