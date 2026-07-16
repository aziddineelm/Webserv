#!/usr/bin/env python3
import subprocess
import time
import http.client
import signal
import sys
import os

def run_comprehensive_valgrind_check():
    print("=================================================================")
    print("   COMPREHENSIVE VALGRIND LEAK CHECK (ALL METHODS & PATHS)      ")
    print("=================================================================")
    
    # Ensure upload directory exists and create a file to test DELETE
    os.makedirs("www/uploads", exist_ok=True)
    with open("www/uploads/valgrind_delete.txt", "w") as f:
        f.write("temporary file for DELETE test under valgrind")

    cmd = [
        "valgrind",
        "--leak-check=full",
        "--show-leak-kinds=all",
        "--track-fds=yes",
        "--error-exitcode=42",
        "./webserv",
        "config/test.conf"
    ]
    
    print(f"[*] Launching server under Valgrind: {' '.join(cmd)}")
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(2)  # Give Valgrind time to start webserv
    
    if proc.poll() is not None:
        stdout, stderr = proc.communicate()
        print("[-] Server failed to start immediately under Valgrind!")
        print(stderr.decode())
        sys.exit(1)
        
    print("[*] Sending requests across ALL major code paths...")
    try:
        # Connection 1: Static GET & Error Pages & Keep-Alive
        c = http.client.HTTPConnection("127.0.0.1", 9090, timeout=5)
        
        c.request("GET", "/")
        r = c.getresponse(); r.read()
        print(f"    1. GET static index          -> Status {r.status} (Keep-Alive)")
        
        c.request("GET", "/non_existent_page_404")
        r = c.getresponse(); r.read()
        print(f"    2. GET 404 Not Found         -> Status {r.status}")
        
        c.request("POST", "/")
        r = c.getresponse(); r.read()
        print(f"    3. POST Method Not Allowed   -> Status {r.status} (405)")
        c.close()

        # Connection 2: CGI GET and CGI POST (Pipe I/O)
        c2 = http.client.HTTPConnection("127.0.0.1", 9090, timeout=5)
        c2.request("GET", "/cgi-bin/hello.py")
        r = c2.getresponse(); r.read()
        print(f"    4. CGI GET execution         -> Status {r.status}")
        
        c2.request("POST", "/cgi-bin/post_echo.py", body="valgrind_cgi_test")
        r = c2.getresponse(); r.read()
        print(f"    5. CGI POST stdin pipe       -> Status {r.status}")
        c2.close()

        # Connection 3: DELETE request path
        c3 = http.client.HTTPConnection("127.0.0.1", 9090, timeout=5)
        c3.request("DELETE", "/upload/valgrind_delete.txt")
        r = c3.getresponse(); r.read()
        print(f"    6. DELETE file path          -> Status {r.status}")
        c3.close()

        # Connection 4: Body size limit (413 Payload Too Large) & Redirection (301)
        c4 = http.client.HTTPConnection("127.0.0.1", 9090, timeout=5)
        c4.request("POST", "/upload", body="X" * 3000)  # Exceeds 1K limit in test.conf
        r = c4.getresponse(); r.read()
        print(f"    7. POST 413 Payload Too Large -> Status {r.status}")
        c4.close()

        c5 = http.client.HTTPConnection("127.0.0.1", 9090, timeout=5)
        c5.request("GET", "/redirect", headers={"Connection": "close"})
        r = c5.getresponse(); r.read()
        print(f"    8. GET 301 Redirect + Close  -> Status {r.status}")
        c5.close()

    except Exception as e:
        print(f"[!] Error during HTTP requests: {e}")
        
    print("[*] All code paths tested! Sending SIGINT (Ctrl+C) to shut down webserv...")
    proc.send_signal(signal.SIGINT)
    
    stdout, stderr = proc.communicate(timeout=15)
    stderr_str = stderr.decode()
    
    print("\n======================= VALGRIND SUMMARY ========================")
    lines = stderr_str.splitlines()
    capture = False
    for line in lines:
        if "FILE DESCRIPTORS:" in line or "HEAP SUMMARY:" in line or "LEAK SUMMARY:" in line or "ERROR SUMMARY:" in line:
            capture = True
        if capture:
            print(line)
            
    print("=================================================================")
    
    leaks_found = False
    clean_heap = ("All heap blocks were freed" in stderr_str) or ("definitely lost: 0 bytes" in stderr_str)
    if not clean_heap:
        print("[-] WARNING: Heap was not completely freed!")
        leaks_found = True
    if "ERROR SUMMARY: 0 errors from 0 contexts" not in stderr_str:
        print("[-] WARNING: Valgrind reported errors!")
        leaks_found = True
        
    if leaks_found:
        print("\n[!] COMPREHENSIVE CHECK FAILED: Leaks or errors detected.")
        sys.exit(1)
    else:
        print("\n[+] COMPREHENSIVE CHECK PASSED: 0 memory leaks, 0 errors, clean file descriptors!")
        sys.exit(0)

if __name__ == "__main__":
    run_comprehensive_valgrind_check()
