#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <string>
#include <ctime>
#include "../http/request/request.hpp"
#include "../http/response/response.hpp"
#include "../cgi/CGIHandler.hpp"

// --------------------------------------------------------------------------
// Client state — where this connection is in the request/response lifecycle
// --------------------------------------------------------------------------

enum ClientState {
	STATE_READING,		// Waiting for complete HTTP request
	STATE_CGI_RUNNING,	// CGI process is running (pipes registered in epoll)
	STATE_WRITING,		// Sending HTTP response
	STATE_DONE			// Ready to close
};

// --------------------------------------------------------------------------
// Client — per-connection data (pure data, no logic)
// --------------------------------------------------------------------------

struct Client {
	int				fd;
	ClientState		state;
	Request			request;		// HTTP request parser (streaming, owns its own buffer)
	Response		response;		// HTTP response stream
	CGIHandler		cgi;			// CGI process handler (Phase 3)
	std::string		writeBuffer;
	size_t			writeOffset;
	time_t			lastActivity;
	int				listenPort;		// Which listening port accepted this client

	// Default constructor
	Client()
		: fd(-1), state(STATE_READING),
		  writeOffset(0), lastActivity(0), listenPort(0) {}

	// Parameterized constructor
	Client(int clientFd, int port)
		: fd(clientFd), state(STATE_READING),
		  writeOffset(0), lastActivity(time(NULL)), listenPort(port) {}
};

#endif
