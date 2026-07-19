#include "SessionManager.hpp"
#include "HttpUtils.hpp"
#include <sstream>
#include <fcntl.h>
#include <unistd.h>

// Static member initialization
std::map<std::string, SessionData> SessionManager::_sessions;

// ============================================================
// Public: attachSession — the ONE method the Router calls
// ============================================================

void SessionManager::attachSession(const Request &req, Response &res) {
	// 1. Extract session ID from the Cookie header (if present)
	std::string cookieHeader = req.getHeader("cookie");
	std::string sessionId = HttpUtils::extractCookieValue(cookieHeader, "session_id");

	// 2. Validate or create
	bool isNewSession = false;
	if (sessionId.empty() || !_isValidSession(sessionId)) {
		sessionId = _createSession();
		if (sessionId.empty())
			return; // OOM defense: map is at MAX_SESSIONS capacity, skip gracefully
		isNewSession = true;
	}

	// 3. Increment visit counter and inject proof header
	int visits = _incrementVisit(sessionId);
	std::ostringstream oss;
	oss << visits;
	res.setHeader("X-Visit-Count", oss.str());

	// 4. Issue Set-Cookie only for new sessions
	if (isNewSession) {
		res.setHeader("Set-Cookie",
			"session_id=" + sessionId + "; Path=/; HttpOnly; Max-Age=3600");
	}

	// 5. CPU optimization: clean up expired sessions every 100 calls
	static int cleanupCounter = 0;
	if (++cleanupCounter > 100) {
		_cleanupExpiredSessions(3600);
		cleanupCounter = 0;
	}
}

// ============================================================
// Private: Session CRUD
// ============================================================

std::string SessionManager::_createSession() {
	// OOM protection: refuse if at capacity
	if (_sessions.size() >= MAX_SESSIONS) {
		_cleanupExpiredSessions(3600);
		if (_sessions.size() >= MAX_SESSIONS)
			return "";
	}

	std::string id = _generateRandomId(16);
	SessionData data;
	data.createdAt = time(NULL);
	data.lastAccessed = data.createdAt;
	data.visitCount = 0;
	_sessions[id] = data;
	return id;
}

bool SessionManager::_isValidSession(const std::string &sessionId) {
	std::map<std::string, SessionData>::iterator it = _sessions.find(sessionId);
	if (it == _sessions.end())
		return false;
	// Update last accessed timestamp
	it->second.lastAccessed = time(NULL);
	return true;
}

int SessionManager::_incrementVisit(const std::string &sessionId) {
	std::map<std::string, SessionData>::iterator it = _sessions.find(sessionId);
	if (it == _sessions.end())
		return 0;
	it->second.visitCount++;
	return it->second.visitCount;
}

void SessionManager::_cleanupExpiredSessions(time_t maxAgeSeconds) {
	time_t now = time(NULL);
	std::map<std::string, SessionData>::iterator it = _sessions.begin();
	while (it != _sessions.end()) {
		if (now - it->second.lastAccessed > maxAgeSeconds) {
			std::map<std::string, SessionData>::iterator toErase = it;
			++it;
			_sessions.erase(toErase);
		} else {
			++it;
		}
	}
}

// ============================================================
// Private: Secure Random ID Generator (using /dev/urandom)
// ============================================================

std::string SessionManager::_generateRandomId(size_t length) {
	std::string id;
	id.reserve(length * 2);

	int fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0) {
		// Fallback: use time + address-based pseudo-random if /dev/urandom unavailable
		std::ostringstream oss;
		oss << time(NULL) << _sessions.size();
		return oss.str();
	}

	unsigned char buf[32];
	size_t bytesNeeded = length;
	if (bytesNeeded > sizeof(buf))
		bytesNeeded = sizeof(buf);

	ssize_t bytesRead = read(fd, buf, bytesNeeded);
	close(fd);

	if (bytesRead <= 0) {
		std::ostringstream oss;
		oss << time(NULL) << _sessions.size();
		return oss.str();
	}

	// Convert raw bytes to hex string
	const char hex[] = "0123456789abcdef";
	for (ssize_t i = 0; i < bytesRead; ++i) {
		id += hex[(buf[i] >> 4) & 0x0F];
		id += hex[buf[i] & 0x0F];
	}

	return id;
}
