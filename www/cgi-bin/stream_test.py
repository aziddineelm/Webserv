#!/usr/bin/env python3
import sys
import time

print("Status: 200 OK")
print("Content-Type: text/plain")
print("")
sys.stdout.flush()

for i in range(5, 0, -1):
    print(f"Streaming data chunk: {i}...")
    sys.stdout.flush()
    time.sleep(1)

print("Stream completed successfully!")
sys.stdout.flush()
