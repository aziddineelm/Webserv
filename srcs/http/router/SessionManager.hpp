#ifndef SESSION_MANAGER_HPP
#define SESSION_MANAGER_HPP

#include <string>
#include <map>
#include <ctime>
#include "../request/request.hpp"
#include "../response/response.hpp"

// Holds data for a single user session
struct SessionData {
	time_t createdAt;
	time_t lastAccessed;
	int visitCount;
};

// Bonus: Centralized, static session store.
// The Router only calls attachSession() — all cookie/map logic is encapsulated here.
class SessionManager {
public:
	// The ONLY method the Router needs to call
	static void attachSession(const Request &req, Response &res);

private:
	static std::map<std::string, SessionData> _sessions;
	static const size_t MAX_SESSIONS = 10000;

	static std::string _createSession();
	static bool _isValidSession(const std::string &sessionId);
	static int  _incrementVisit(const std::string &sessionId);
	static void _cleanupExpiredSessions(time_t maxAgeSeconds);
	static std::string _generateRandomId(size_t length);
};

#endif
