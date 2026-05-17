#include "request/request.hpp"
#include <iostream>
#include <cassert>

static int passed = 0;
static int failed = 0;

static void check(const std::string& name, bool cond) {
	if (cond) { std::cout << "  ✅ " << name << std::endl; passed++; }
	else { std::cout << "  ❌ " << name << std::endl; failed++; }
}

// === BASIC TESTS ===

static void testSimpleGet() {
	std::cout << "\n[1] Simple GET" << std::endl;
	Request r;
	r.feed("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
	check("complete", r.isComplete());
	check("method", r.getMethod() == "GET");
	check("path", r.getPath() == "/");
	check("host", r.getHeader("host") == "localhost");
	check("body empty", r.getBody().empty());
}

static void testPostBody() {
	std::cout << "\n[2] POST with body" << std::endl;
	Request r;
	r.feed("POST /api HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\n\r\nhello");
	check("complete", r.isComplete());
	check("body", r.getBody() == "hello");
}

static void testDelete() {
	std::cout << "\n[3] DELETE" << std::endl;
	Request r;
	r.feed("DELETE /file.txt HTTP/1.1\r\nHost: x\r\n\r\n");
	check("complete", r.isComplete());
	check("method", r.getMethod() == "DELETE");
}

static void testQueryString() {
	std::cout << "\n[4] Query string" << std::endl;
	Request r;
	r.feed("GET /search?q=hello&p=2 HTTP/1.1\r\nHost: x\r\n\r\n");
	check("path", r.getPath() == "/search");
	check("query", r.getQueryString() == "q=hello&p=2");
}

// === PARTIAL DATA ===

static void testPartialRequestLine() {
	std::cout << "\n[5] Partial request line" << std::endl;
	Request r;
	r.feed("GET /in");
	check("not complete", !r.isComplete() && !r.hasError());
	r.feed("dex HTTP/1.1\r\nHost: x\r\n\r\n");
	check("complete", r.isComplete());
	check("path", r.getPath() == "/index");
}

static void testPartialHeaders() {
	std::cout << "\n[6] Partial headers" << std::endl;
	Request r;
	r.feed("GET / HTTP/1.1\r\nHost: loc");
	check("not complete", !r.isComplete());
	r.feed("alhost\r\n\r\n");
	check("complete", r.isComplete());
}

static void testPartialBody() {
	std::cout << "\n[7] Partial body" << std::endl;
	Request r;
	r.feed("POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 10\r\n\r\nhello");
	check("not complete (5/10)", !r.isComplete());
	r.feed("world");
	check("complete", r.isComplete());
	check("body", r.getBody() == "helloworld");
}

static void testByteByByte() {
	std::cout << "\n[8] Byte-by-byte feed" << std::endl;
	Request r;
	std::string full = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
	for (size_t i = 0; i < full.size(); i++)
		r.feed(std::string(1, full[i]));
	check("complete", r.isComplete());
	check("method", r.getMethod() == "GET");
}

// === CHUNKED ENCODING ===

static void testChunkedBasic() {
	std::cout << "\n[9] Chunked basic" << std::endl;
	Request r;
	r.feed("POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n");
	r.feed("5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n");
	check("complete", r.isComplete());
	check("body", r.getBody() == "hello world");
}

static void testChunkedPartial() {
	std::cout << "\n[10] Chunked partial" << std::endl;
	Request r;
	r.feed("POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n3\r\n");
	check("not complete", !r.isComplete());
	r.feed("abc\r\n0\r\n\r\n");
	check("complete", r.isComplete());
	check("body", r.getBody() == "abc");
}

static void testChunkedHexUppercase() {
	std::cout << "\n[11] Chunked hex uppercase (A = 10)" << std::endl;
	Request r;
	r.feed("POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n");
	r.feed("A\r\n0123456789\r\n0\r\n\r\n");
	check("complete", r.isComplete());
	check("body len=10", r.getBody().size() == 10);
	check("body", r.getBody() == "0123456789");
}

static void testChunkedWithExtension() {
	std::cout << "\n[12] Chunked with extension (;ext=val)" << std::endl;
	Request r;
	r.feed("POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n");
	r.feed("5;ext=val\r\nhello\r\n0\r\n\r\n");
	check("complete", r.isComplete());
	check("body", r.getBody() == "hello");
}

static void testChunkedOverridesContentLength() {
	std::cout << "\n[13] Chunked overrides Content-Length" << std::endl;
	Request r;
	r.feed("POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 999\r\nTransfer-Encoding: chunked\r\n\r\n");
	r.feed("3\r\nabc\r\n0\r\n\r\n");
	check("complete", r.isComplete());
	check("body=abc (not 999 bytes)", r.getBody() == "abc");
}

static void testChunkedEmptyBody() {
	std::cout << "\n[14] Chunked empty body (0 immediately)" << std::endl;
	Request r;
	r.feed("POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n");
	check("complete", r.isComplete());
	check("body empty", r.getBody().empty());
}

// === CONTENT-LENGTH EDGE CASES ===

static void testContentLengthZero() {
	std::cout << "\n[15] Content-Length: 0" << std::endl;
	Request r;
	r.feed("POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n");
	check("complete", r.isComplete());
	check("body empty", r.getBody().empty());
}

static void testContentLengthExact() {
	std::cout << "\n[16] Body exactly Content-Length" << std::endl;
	Request r;
	r.feed("POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 3\r\n\r\nabcEXTRA");
	check("complete", r.isComplete());
	check("body=abc (not abcEXTRA)", r.getBody() == "abc");
}

// === VALIDATION / ERROR CASES ===

static void testBadMethod() {
	std::cout << "\n[17] Bad method (PUT)" << std::endl;
	Request r;
	r.feed("PUT / HTTP/1.1\r\nHost: x\r\n\r\n");
	check("error", r.hasError());
	check("code=400", r.getErrorCode() == 400);
}

static void testBadVersion() {
	std::cout << "\n[18] Bad version (HTTP/2.0)" << std::endl;
	Request r;
	r.feed("GET / HTTP/2.0\r\nHost: x\r\n\r\n");
	check("error", r.hasError());
}

static void testMissingHost() {
	std::cout << "\n[19] Missing Host (HTTP/1.1)" << std::endl;
	Request r;
	r.feed("GET / HTTP/1.1\r\nConnection: close\r\n\r\n");
	check("error", r.hasError());
}

static void testMissingHostHttp10OK() {
	std::cout << "\n[20] Missing Host (HTTP/1.0) - OK" << std::endl;
	Request r;
	r.feed("GET / HTTP/1.0\r\n\r\n");
	check("complete (no error)", r.isComplete() && !r.hasError());
}

static void testMalformedRequestLine2Tokens() {
	std::cout << "\n[21] Request line: 2 tokens" << std::endl;
	Request r;
	r.feed("GET /index.html\r\n\r\n");
	check("error", r.hasError());
}

static void testMalformedRequestLine4Tokens() {
	std::cout << "\n[22] Request line: 4 tokens" << std::endl;
	Request r;
	r.feed("GET / HTTP/1.1 extra\r\nHost: x\r\n\r\n");
	check("error", r.hasError());
}

static void testEmptyRequestLine() {
	std::cout << "\n[23] Empty request line (just CRLF)" << std::endl;
	Request r;
	// Leading CRLF should be skipped (RFC 2616 §4.1)
	r.feed("\r\nGET / HTTP/1.1\r\nHost: x\r\n\r\n");
	check("complete (skipped leading CRLF)", r.isComplete());
}

static void testBadContentLength() {
	std::cout << "\n[24] Bad Content-Length (text)" << std::endl;
	Request r;
	r.feed("POST / HTTP/1.1\r\nHost: x\r\nContent-Length: abc\r\n\r\n");
	check("error", r.hasError());
}

static void testNegativeContentLength() {
	std::cout << "\n[25] Negative Content-Length" << std::endl;
	Request r;
	r.feed("POST / HTTP/1.1\r\nHost: x\r\nContent-Length: -5\r\n\r\n");
	check("error", r.hasError());
}

static void testEmptyContentLength() {
	std::cout << "\n[26] Empty Content-Length value" << std::endl;
	Request r;
	r.feed("POST / HTTP/1.1\r\nHost: x\r\nContent-Length: \r\n\r\n");
	check("error", r.hasError());
}

static void testHeaderNoColon() {
	std::cout << "\n[27] Header without colon" << std::endl;
	Request r;
	r.feed("GET / HTTP/1.1\r\nBadHeader\r\nHost: x\r\n\r\n");
	check("error", r.hasError());
}

static void testPathNoSlash() {
	std::cout << "\n[28] Path without leading /" << std::endl;
	Request r;
	r.feed("GET index.html HTTP/1.1\r\nHost: x\r\n\r\n");
	check("error", r.hasError());
}

static void testBadChunkedHex() {
	std::cout << "\n[29] Bad chunked hex (ZZZ)" << std::endl;
	Request r;
	r.feed("POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\nZZZ\r\n");
	check("error", r.hasError());
}

static void testOnlyMethod() {
	std::cout << "\n[30] Only method (1 token)" << std::endl;
	Request r;
	r.feed("GET\r\n\r\n");
	check("error", r.hasError());
}

// === CASE INSENSITIVITY ===

static void testCaseInsensitiveHeaders() {
	std::cout << "\n[31] Case-insensitive headers" << std::endl;
	Request r;
	r.feed("GET / HTTP/1.1\r\nHOST: example.com\r\nContent-TYPE: text/html\r\n\r\n");
	check("host lowercase query", r.getHeader("host") == "example.com");
	check("host uppercase query", r.getHeader("HOST") == "example.com");
	check("content-type", r.getHeader("content-type") == "text/html");
}

// === KEEP-ALIVE / RESET ===

static void testKeepAliveReset() {
	std::cout << "\n[32] Keep-alive reset" << std::endl;
	Request r;
	r.feed("GET /page1 HTTP/1.1\r\nHost: x\r\n\r\n");
	check("first complete", r.isComplete());
	r.reset();
	r.feed("DELETE /page2 HTTP/1.1\r\nHost: x\r\n\r\n");
	check("second complete", r.isComplete());
	check("second method", r.getMethod() == "DELETE");
	check("second path", r.getPath() == "/page2");
}

static void testKeepAliveLeftoverData() {
	std::cout << "\n[33] Keep-alive: leftover data in buffer" << std::endl;
	Request r;
	// Two requests pipelined in one feed
	r.feed("GET /a HTTP/1.1\r\nHost: x\r\n\r\nGET /b HTTP/1.1\r\nHost: x\r\n\r\n");
	check("first complete", r.isComplete());
	check("first path=/a", r.getPath() == "/a");
	r.reset();
	r.feed(""); // Trigger parsing of leftover buffer
	check("second complete", r.isComplete());
	check("second path=/b", r.getPath() == "/b");
}

static void testKeepAliveBodyThenNext() {
	std::cout << "\n[34] Keep-alive: POST body + next request in buffer" << std::endl;
	Request r;
	r.feed("POST /x HTTP/1.1\r\nHost: x\r\nContent-Length: 3\r\n\r\nabcGET /y HTTP/1.1\r\nHost: x\r\n\r\n");
	check("first complete", r.isComplete());
	check("body=abc", r.getBody() == "abc");
	r.reset();
	r.feed("");
	check("second complete", r.isComplete());
	check("second path=/y", r.getPath() == "/y");
}

// === FEED AFTER COMPLETE/ERROR ===

static void testFeedAfterComplete() {
	std::cout << "\n[35] Feed after COMPLETE is ignored" << std::endl;
	Request r;
	r.feed("GET / HTTP/1.1\r\nHost: x\r\n\r\n");
	check("complete", r.isComplete());
	r.feed("GARBAGE THAT SHOULD BE IGNORED");
	check("still complete", r.isComplete());
	check("no error", !r.hasError());
}

static void testFeedAfterError() {
	std::cout << "\n[36] Feed after ERROR is ignored" << std::endl;
	Request r;
	r.feed("INVALID\r\n\r\n");
	check("error", r.hasError());
	r.feed("GET / HTTP/1.1\r\nHost: x\r\n\r\n");
	check("still error", r.hasError());
}

// === MULTIPLE HEADERS & EDGE FORMATTING ===

static void testMultipleHeaders() {
	std::cout << "\n[37] Many headers" << std::endl;
	Request r;
	r.feed("GET / HTTP/1.1\r\nHost: x\r\nAccept: */*\r\nUser-Agent: test\r\nConnection: close\r\n\r\n");
	check("complete", r.isComplete());
	check("accept", r.getHeader("accept") == "*/*");
	check("user-agent", r.getHeader("user-agent") == "test");
	check("connection", r.getHeader("connection") == "close");
}

static void testHeaderValueWithColon() {
	std::cout << "\n[38] Header value containing colon" << std::endl;
	Request r;
	r.feed("GET / HTTP/1.1\r\nHost: localhost:8080\r\n\r\n");
	check("complete", r.isComplete());
	check("host with port", r.getHeader("host") == "localhost:8080");
}

static void testHeaderWithExtraSpaces() {
	std::cout << "\n[39] Header with extra spaces in value" << std::endl;
	Request r;
	r.feed("GET / HTTP/1.1\r\nHost:   localhost   \r\n\r\n");
	check("complete", r.isComplete());
	check("value trimmed", r.getHeader("host") == "localhost");
}

static void testDuplicateHeaders() {
	std::cout << "\n[40] Duplicate headers (last wins)" << std::endl;
	Request r;
	r.feed("GET / HTTP/1.1\r\nHost: first\r\nHost: second\r\n\r\n");
	check("complete", r.isComplete());
	check("last value wins", r.getHeader("host") == "second");
}

// === COPY / ASSIGNMENT ===

static void testCopyConstructor() {
	std::cout << "\n[41] Copy constructor" << std::endl;
	Request r1;
	r1.feed("GET /copy HTTP/1.1\r\nHost: x\r\n\r\n");
	Request r2(r1);
	check("copy complete", r2.isComplete());
	check("copy path", r2.getPath() == "/copy");
	check("copy method", r2.getMethod() == "GET");
}

static void testAssignmentOperator() {
	std::cout << "\n[42] Assignment operator" << std::endl;
	Request r1;
	r1.feed("POST /assign HTTP/1.1\r\nHost: x\r\nContent-Length: 2\r\n\r\nhi");
	Request r2;
	r2 = r1;
	check("assign complete", r2.isComplete());
	check("assign path", r2.getPath() == "/assign");
	check("assign body", r2.getBody() == "hi");
}

// === URI EDGE CASES ===

static void testRootPath() {
	std::cout << "\n[43] Root path /" << std::endl;
	Request r;
	r.feed("GET / HTTP/1.1\r\nHost: x\r\n\r\n");
	check("path=/", r.getPath() == "/");
	check("query empty", r.getQueryString().empty());
}

static void testQueryOnly() {
	std::cout << "\n[44] URI = /?key=val (query on root)" << std::endl;
	Request r;
	r.feed("GET /?key=val HTTP/1.1\r\nHost: x\r\n\r\n");
	check("path=/", r.getPath() == "/");
	check("query=key=val", r.getQueryString() == "key=val");
}

static void testEmptyQuery() {
	std::cout << "\n[45] URI = /path? (empty query)" << std::endl;
	Request r;
	r.feed("GET /path? HTTP/1.1\r\nHost: x\r\n\r\n");
	check("path=/path", r.getPath() == "/path");
	check("query empty", r.getQueryString().empty());
}

static void testLongPath() {
	std::cout << "\n[46] Long path (1000 chars)" << std::endl;
	Request r;
	std::string longPath = "/";
	for (int i = 0; i < 999; i++) longPath += "a";
	r.feed("GET " + longPath + " HTTP/1.1\r\nHost: x\r\n\r\n");
	check("complete", r.isComplete());
	check("path correct", r.getPath() == longPath);
}

// === MAIN ===

int main() {
	std::cout << "==========================================" << std::endl;
	std::cout << "  Phase 1 — Edge Case Tests" << std::endl;
	std::cout << "==========================================" << std::endl;

	testSimpleGet(); testPostBody(); testDelete(); testQueryString();
	testPartialRequestLine(); testPartialHeaders(); testPartialBody(); testByteByByte();
	testChunkedBasic(); testChunkedPartial(); testChunkedHexUppercase();
	testChunkedWithExtension(); testChunkedOverridesContentLength(); testChunkedEmptyBody();
	testContentLengthZero(); testContentLengthExact();
	testBadMethod(); testBadVersion(); testMissingHost(); testMissingHostHttp10OK();
	testMalformedRequestLine2Tokens(); testMalformedRequestLine4Tokens(); testEmptyRequestLine();
	testBadContentLength(); testNegativeContentLength(); testEmptyContentLength();
	testHeaderNoColon(); testPathNoSlash(); testBadChunkedHex(); testOnlyMethod();
	testCaseInsensitiveHeaders();
	testKeepAliveReset(); testKeepAliveLeftoverData(); testKeepAliveBodyThenNext();
	testFeedAfterComplete(); testFeedAfterError();
	testMultipleHeaders(); testHeaderValueWithColon(); testHeaderWithExtraSpaces(); testDuplicateHeaders();
	testCopyConstructor(); testAssignmentOperator();
	testRootPath(); testQueryOnly(); testEmptyQuery(); testLongPath();

	std::cout << "\n==========================================" << std::endl;
	std::cout << "  Results: " << passed << " passed, " << failed << " failed" << std::endl;
	std::cout << "==========================================" << std::endl;
	return failed > 0 ? 1 : 0;
}
