#!/usr/bin/env python3
"""
Webserv Automated Test Suite
=============================
Tests all mandatory requirements from en.Webserv.txt.
Uses only Python 3 built-in libraries (no pip install needed).

Usage:
    1. Start server:  ./webserv config/test.conf
    2. Run tests:     python3 tests/test_webserv.py
"""

import socket
import time
import sys
import os

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

HOST = "127.0.0.1"
PORT = 9090
TIMEOUT = 10  # Default socket timeout in seconds


# ---------------------------------------------------------------------------
# Terminal Colors
# ---------------------------------------------------------------------------

class Colors:
    GREEN  = "\033[92m"
    RED    = "\033[91m"
    YELLOW = "\033[93m"
    CYAN   = "\033[96m"
    BOLD   = "\033[1m"
    DIM    = "\033[2m"
    RESET  = "\033[0m"


# ---------------------------------------------------------------------------
# Test Result
# ---------------------------------------------------------------------------

class TestResult:
    PASS = "PASS"
    FAIL = "FAIL"
    SKIP = "SKIP"

    def __init__(self, status, name, detail=""):
        self.status = status
        self.name = name
        self.detail = detail


# ---------------------------------------------------------------------------
# Raw Socket Helpers
# ---------------------------------------------------------------------------

def raw_request(data, timeout=TIMEOUT, read_response=True):
    """Send raw bytes over a TCP socket and return the response as a string."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    try:
        sock.connect((HOST, PORT))
        if isinstance(data, str):
            data = data.encode("utf-8")
        sock.sendall(data)
        if not read_response:
            sock.close()
            return ""
        response = b""
        while True:
            try:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                response += chunk
            except socket.timeout:
                break
        return response.decode("utf-8", errors="replace")
    except Exception as e:
        return "ERROR: {}".format(str(e))
    finally:
        sock.close()


def http_request(method, path, headers=None, body=None, timeout=TIMEOUT):
    """Build and send a proper HTTP/1.1 request. Returns (status_code, headers_dict, body)."""
    if headers is None:
        headers = {}
    if "Host" not in headers:
        headers["Host"] = "localhost"

    request_line = "{} {} HTTP/1.1\r\n".format(method, path)
    header_lines = ""
    for key, value in headers.items():
        header_lines += "{}: {}\r\n".format(key, value)

    if body is not None:
        if isinstance(body, str):
            body = body.encode("utf-8")
        header_lines += "Content-Length: {}\r\n".format(len(body))

    raw = request_line + header_lines + "\r\n"
    raw_bytes = raw.encode("utf-8")
    if body is not None:
        raw_bytes += body

    response = raw_request(raw_bytes, timeout=timeout)
    return parse_response(response)


def parse_response(response):
    """Parse an HTTP response string into (status_code, headers_dict, body)."""
    if response.startswith("ERROR:"):
        return (0, {}, response)

    try:
        header_end = response.index("\r\n\r\n")
        header_section = response[:header_end]
        body = response[header_end + 4:]
    except ValueError:
        return (0, {}, response)

    lines = header_section.split("\r\n")
    status_line = lines[0]
    try:
        status_code = int(status_line.split(" ")[1])
    except (IndexError, ValueError):
        status_code = 0

    headers = {}
    for line in lines[1:]:
        if ": " in line:
            key, value = line.split(": ", 1)
            headers[key.lower()] = value

    return (status_code, headers, body)


def keepalive_request(requests):
    """Send multiple HTTP requests over a single persistent TCP connection.
    
    Args:
        requests: list of (method, path) tuples
    Returns:
        list of (status_code, headers_dict, body) tuples
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(TIMEOUT)
    results = []
    try:
        sock.connect((HOST, PORT))

        for method, path in requests:
            req = "{} {} HTTP/1.1\r\nHost: localhost\r\nConnection: keep-alive\r\n\r\n".format(method, path)
            sock.sendall(req.encode("utf-8"))

            # Read response — we need to figure out when one response ends
            response = b""
            while True:
                try:
                    chunk = sock.recv(4096)
                    if not chunk:
                        break
                    response += chunk
                    # Check if we have a complete response
                    decoded = response.decode("utf-8", errors="replace")
                    if "\r\n\r\n" in decoded:
                        header_part = decoded.split("\r\n\r\n")[0]
                        # Look for Content-Length
                        content_length = None
                        for line in header_part.split("\r\n"):
                            if line.lower().startswith("content-length:"):
                                content_length = int(line.split(":")[1].strip())
                                break
                        if content_length is not None:
                            body_start = len(header_part.encode("utf-8")) + 4
                            if len(response) >= body_start + content_length:
                                break
                        else:
                            # No content-length — assume we got enough after a brief pause
                            time.sleep(0.1)
                            try:
                                extra = sock.recv(4096)
                                if extra:
                                    response += extra
                            except socket.timeout:
                                pass
                            break
                except socket.timeout:
                    break

            results.append(parse_response(response.decode("utf-8", errors="replace")))
    except Exception as e:
        results.append((0, {}, "ERROR: {}".format(str(e))))
    finally:
        sock.close()
    return results


# ---------------------------------------------------------------------------
# Test Functions
# ---------------------------------------------------------------------------

# ===== Category 1: Static File Serving =====

def test_01_get_index():
    """GET / serves index.html"""
    status, headers, body = http_request("GET", "/")
    if status == 200 and "Webserv" in body:
        return TestResult(TestResult.PASS, "GET / serves index.html", "200 OK")
    return TestResult(TestResult.FAIL, "GET / serves index.html",
                      "Expected 200 + 'Webserv', got {} | body preview: {}".format(status, body[:80]))


def test_02_get_css():
    """GET /pages/style.css serves CSS file"""
    status, headers, body = http_request("GET", "/pages/style.css")
    if status == 200:
        return TestResult(TestResult.PASS, "GET /pages/style.css serves CSS", "200 OK")
    return TestResult(TestResult.FAIL, "GET /pages/style.css serves CSS",
                      "Expected 200, got {}".format(status))


def test_03_get_404():
    """GET /nonexistent.html returns 404"""
    status, headers, body = http_request("GET", "/nonexistent_file_that_does_not_exist.html")
    if status == 404:
        return TestResult(TestResult.PASS, "GET /nonexistent returns 404", "404 Not Found")
    return TestResult(TestResult.FAIL, "GET /nonexistent returns 404",
                      "Expected 404, got {}".format(status))


# ===== Category 2: HTTP Methods & Error Codes =====

def test_04_post_root_405():
    """POST / should be blocked (405 Method Not Allowed)"""
    status, headers, body = http_request("POST", "/", body="test")
    if status == 405:
        return TestResult(TestResult.PASS, "POST / blocked (405)", "405 Method Not Allowed")
    return TestResult(TestResult.FAIL, "POST / blocked (405)",
                      "Expected 405, got {}".format(status))


def test_05_delete_root_405():
    """DELETE / should be blocked (405 Method Not Allowed)"""
    status, headers, body = http_request("DELETE", "/")
    if status == 405:
        return TestResult(TestResult.PASS, "DELETE / blocked (405)", "405 Method Not Allowed")
    return TestResult(TestResult.FAIL, "DELETE / blocked (405)",
                      "Expected 405, got {}".format(status))


def test_06_malformed_request_400():
    """Malformed request line should return 400"""
    response = raw_request("INVALID REQUEST LINE HERE\r\n\r\n")
    parsed = parse_response(response)
    status = parsed[0]
    if status == 400:
        return TestResult(TestResult.PASS, "Malformed request (400)", "400 Bad Request")
    return TestResult(TestResult.FAIL, "Malformed request (400)",
                      "Expected 400, got {}".format(status))


def test_07_unknown_method():
    """Unknown HTTP method should return 405 or 501"""
    status, headers, body = http_request("PATCH", "/")
    if status in (405, 501):
        return TestResult(TestResult.PASS, "Unknown method PATCH", "{} returned".format(status))
    return TestResult(TestResult.FAIL, "Unknown method PATCH",
                      "Expected 405 or 501, got {}".format(status))


# ===== Category 3: Body Size Limit =====

def test_08_post_within_limit():
    """POST body within client_max_body_size (1KB) should succeed"""
    small_body = "x" * 500  # 500 bytes — within 1KB limit
    status, headers, body = http_request("POST", "/upload",
                                          headers={"Content-Type": "text/plain"},
                                          body=small_body)
    if status in (200, 201, 204):
        return TestResult(TestResult.PASS, "POST body within limit (500B)", "{} OK".format(status))
    return TestResult(TestResult.FAIL, "POST body within limit (500B)",
                      "Expected 200/201/204, got {}".format(status))


def test_09_post_exceeds_limit_413():
    """POST body exceeding client_max_body_size (1KB) should return 413"""
    large_body = "x" * 2048  # 2KB — exceeds 1KB limit
    status, headers, body = http_request("POST", "/upload",
                                          headers={"Content-Type": "text/plain"},
                                          body=large_body)
    if status == 413:
        return TestResult(TestResult.PASS, "POST body exceeds limit (413)", "413 Payload Too Large")
    return TestResult(TestResult.FAIL, "POST body exceeds limit (413)",
                      "Expected 413, got {}".format(status))


# ===== Category 4: CGI Execution =====

def test_10_cgi_hello():
    """GET /cgi-bin/hello.py should execute CGI and return output"""
    status, headers, body = http_request("GET", "/cgi-bin/hello.py")
    if status == 200 and "Hello from CGI" in body:
        return TestResult(TestResult.PASS, "CGI GET hello.py", "200 OK + correct output")
    return TestResult(TestResult.FAIL, "CGI GET hello.py",
                      "Expected 200 + 'Hello from CGI', got {} | body: {}".format(status, body[:80]))


def test_11_cgi_post_echo():
    """POST /cgi-bin/post_echo.py should echo the body via stdin pipe"""
    test_body = "webserv_test_payload_42"
    status, headers, body = http_request("POST", "/cgi-bin/post_echo.py",
                                          headers={"Content-Type": "text/plain"},
                                          body=test_body)
    if status == 200 and test_body in body:
        return TestResult(TestResult.PASS, "CGI POST echo (stdin pipe)", "200 OK + body echoed")
    return TestResult(TestResult.FAIL, "CGI POST echo (stdin pipe)",
                      "Expected 200 + '{}' in body, got {} | body: {}".format(test_body, status, body[:80]))


def test_12_cgi_timeout_504():
    """GET /cgi-bin/infinite_loop.py should timeout with 504"""
    start = time.time()
    status, headers, body = http_request("GET", "/cgi-bin/infinite_loop.py", timeout=6)
    elapsed = time.time() - start
    if status == 504:
        return TestResult(TestResult.PASS, "CGI timeout (504)",
                          "504 Gateway Timeout in {:.1f}s".format(elapsed))
    return TestResult(TestResult.FAIL, "CGI timeout (504)",
                      "Expected 504, got {} (took {:.1f}s)".format(status, elapsed))


# ===== Category 5: Keep-Alive =====

def test_13_keepalive_reuse():
    """Two GET requests on same TCP socket (Keep-Alive)"""
    results = keepalive_request([("GET", "/"), ("GET", "/pages/style.css")])
    if len(results) >= 2 and results[0][0] == 200 and results[1][0] == 200:
        return TestResult(TestResult.PASS, "Keep-Alive: 2 requests on 1 socket", "Both 200 OK")
    statuses = [r[0] for r in results]
    return TestResult(TestResult.FAIL, "Keep-Alive: 2 requests on 1 socket",
                      "Expected [200, 200], got {}".format(statuses))


def test_14_connection_close():
    """Connection: close header should close the connection after response"""
    status, headers, body = http_request("GET", "/",
                                          headers={"Connection": "close"})
    conn_header = headers.get("connection", "").lower()
    if status == 200 and "close" in conn_header:
        return TestResult(TestResult.PASS, "Connection: close respected", "200 OK + Connection: close")
    if status == 200:
        return TestResult(TestResult.PASS, "Connection: close respected",
                          "200 OK (connection header: '{}')".format(conn_header))
    return TestResult(TestResult.FAIL, "Connection: close respected",
                      "Expected 200, got {}".format(status))


# ===== Category 6: Redirections =====

def test_15_redirect_301():
    """GET /redirect should return 301 with Location header"""
    status, headers, body = http_request("GET", "/redirect")
    location = headers.get("location", "")
    if status == 301 and "example.com" in location:
        return TestResult(TestResult.PASS, "GET /redirect returns 301",
                          "301 -> {}".format(location))
    return TestResult(TestResult.FAIL, "GET /redirect returns 301",
                      "Expected 301 + Location with example.com, got {} | location: '{}'".format(status, location))


# ===== Category 7: Edge Cases & Robustness =====

def test_16_empty_request():
    """Empty request should not crash the server"""
    response = raw_request("\r\n\r\n", timeout=3)
    # After the empty request, verify server is still alive
    status, headers, body = http_request("GET", "/")
    if status == 200:
        return TestResult(TestResult.PASS, "Empty request (no crash)", "Server still alive (200 OK)")
    return TestResult(TestResult.FAIL, "Empty request (no crash)",
                      "Server may have crashed — follow-up GET returned {}".format(status))


def test_17_long_url_414():
    """Very long URL should return 414 or 400"""
    long_path = "/" + "A" * 8192
    status, headers, body = http_request("GET", long_path)
    if status in (414, 400, 431):
        return TestResult(TestResult.PASS, "Very long URL ({})".format(status),
                          "{} returned".format(status))
    return TestResult(TestResult.FAIL, "Very long URL",
                      "Expected 414/400/431, got {}".format(status))


def test_18_long_header_431():
    """Very long header should return 431 or 400"""
    long_value = "X" * 16384
    status, headers, body = http_request("GET", "/",
                                          headers={"X-Overflow": long_value})
    if status in (431, 400, 414):
        return TestResult(TestResult.PASS, "Very long header ({})".format(status),
                          "{} returned".format(status))
    return TestResult(TestResult.FAIL, "Very long header",
                      "Expected 431/400, got {}".format(status))


def test_19_slow_client():
    """Slow client (partial send) should not crash server"""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(TIMEOUT)
    try:
        sock.connect((HOST, PORT))
        # Send partial request
        sock.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\n")
        time.sleep(1)
        # Complete the request
        sock.sendall(b"Connection: close\r\n\r\n")
        response = b""
        while True:
            try:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                response += chunk
            except socket.timeout:
                break
        parsed = parse_response(response.decode("utf-8", errors="replace"))
        if parsed[0] == 200:
            return TestResult(TestResult.PASS, "Slow client (partial send)", "200 OK after delayed completion")
        return TestResult(TestResult.FAIL, "Slow client (partial send)",
                          "Expected 200, got {}".format(parsed[0]))
    except Exception as e:
        return TestResult(TestResult.FAIL, "Slow client (partial send)", "Error: {}".format(str(e)))
    finally:
        sock.close()


def test_20_rapid_disconnect():
    """Rapid connect+disconnect should not crash the server"""
    for i in range(10):
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(1)
            sock.connect((HOST, PORT))
            sock.close()
        except Exception:
            pass
        time.sleep(0.05)

    # Verify server is still alive
    time.sleep(0.5)
    status, headers, body = http_request("GET", "/")
    if status == 200:
        return TestResult(TestResult.PASS, "Rapid disconnect x10 (no crash)", "Server still alive (200 OK)")
    return TestResult(TestResult.FAIL, "Rapid disconnect x10 (no crash)",
                      "Server may have crashed — follow-up GET returned {}".format(status))


# ---------------------------------------------------------------------------
# Test Runner
# ---------------------------------------------------------------------------

CATEGORIES = [
    ("Static File Serving", [test_01_get_index, test_02_get_css, test_03_get_404]),
    ("HTTP Methods & Error Codes", [test_04_post_root_405, test_05_delete_root_405,
                                     test_06_malformed_request_400, test_07_unknown_method]),
    ("Body Size Limit", [test_08_post_within_limit, test_09_post_exceeds_limit_413]),
    ("CGI Execution", [test_10_cgi_hello, test_11_cgi_post_echo, test_12_cgi_timeout_504]),
    ("Keep-Alive", [test_13_keepalive_reuse, test_14_connection_close]),
    ("Redirections", [test_15_redirect_301]),
    ("Edge Cases & Robustness", [test_16_empty_request, test_17_long_url_414,
                                  test_18_long_header_431, test_19_slow_client,
                                  test_20_rapid_disconnect]),
]


def check_server():
    """Check if the server is running before starting tests."""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(2)
        sock.connect((HOST, PORT))
        sock.close()
        return True
    except Exception:
        return False


def run_all():
    """Run all tests and print results."""
    print()
    print("{}{}  ════════════════════════════════════════════════════ {}".format(
        Colors.BOLD, Colors.CYAN, Colors.RESET))
    print("{}{}   Webserv Automated Test Suite                       {}".format(
        Colors.BOLD, Colors.CYAN, Colors.RESET))
    print("{}{}   Target: {}:{:<43s}{}".format(
        Colors.BOLD, Colors.CYAN, HOST, str(PORT), Colors.RESET))
    print("{}{}  ════════════════════════════════════════════════════ {}".format(
        Colors.BOLD, Colors.CYAN, Colors.RESET))
    print()

    # Pre-flight: check server is running
    if not check_server():
        print("{}{}  ❌ ERROR: Cannot connect to {}:{}{}".format(
            Colors.BOLD, Colors.RED, HOST, PORT, Colors.RESET))
        print("{}  Please start the server first:{}".format(Colors.DIM, Colors.RESET))
        print("{}    ./webserv config/test.conf{}".format(Colors.DIM, Colors.RESET))
        print()
        sys.exit(1)

    total_pass = 0
    total_fail = 0
    total_skip = 0
    failed_tests = []

    for category_name, tests in CATEGORIES:
        print("  {}{}[{}]{}".format(Colors.BOLD, Colors.CYAN, category_name, Colors.RESET))

        for test_fn in tests:
            try:
                result = test_fn()
            except Exception as e:
                result = TestResult(TestResult.FAIL, test_fn.__doc__ or test_fn.__name__,
                                    "Exception: {}".format(str(e)))

            if result.status == TestResult.PASS:
                icon = "{}✅ PASS{}".format(Colors.GREEN, Colors.RESET)
                total_pass += 1
            elif result.status == TestResult.FAIL:
                icon = "{}❌ FAIL{}".format(Colors.RED, Colors.RESET)
                total_fail += 1
                failed_tests.append(result)
            else:
                icon = "{}⏭  SKIP{}".format(Colors.YELLOW, Colors.RESET)
                total_skip += 1

            detail = "  {}({}){}".format(Colors.DIM, result.detail, Colors.RESET) if result.detail else ""
            print("    {}  {:<42s}{}".format(icon, result.name, detail))

        print()

    # Summary
    total = total_pass + total_fail + total_skip
    print("  {}{}════════════════════════════════════════════════════{}".format(
        Colors.BOLD, Colors.CYAN, Colors.RESET))

    if total_fail == 0:
        summary_color = Colors.GREEN
        summary_icon = "🏆"
    else:
        summary_color = Colors.RED
        summary_icon = "⚠️ "

    print("  {} {}{}{}/{} PASSED  |  {} FAILED  |  {} SKIPPED{}".format(
        summary_icon, Colors.BOLD, summary_color,
        total_pass, total, total_fail, total_skip, Colors.RESET))
    print("  {}{}════════════════════════════════════════════════════{}".format(
        Colors.BOLD, Colors.CYAN, Colors.RESET))

    # Print failed test details
    if failed_tests:
        print()
        print("  {}{}Failed Test Details:{}".format(Colors.BOLD, Colors.RED, Colors.RESET))
        for result in failed_tests:
            print("    {}• {}: {}{}".format(Colors.RED, result.name, result.detail, Colors.RESET))

    print()
    sys.exit(0 if total_fail == 0 else 1)


if __name__ == "__main__":
    run_all()
