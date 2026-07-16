#!/usr/bin/env python3
import sys
import os

body = sys.stdin.read()
print("Content-Type: text/plain")
print("Content-Length: {}".format(len(body)))
print()
sys.stdout.write(body)
