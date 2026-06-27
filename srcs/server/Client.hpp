#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <string>
#include <ctime>
#include "../http/request/request.hpp"
#include "../http/response/response.hpp"

// --------------------------------------------------------------------------
// FD type — determines how the event loop handles events on this FD
// --------------------------------------------------------------------------

enum FdType {
	FD_LISTEN,		// Listening socket → accept() on POLLIN
	FD_CLIENT,		// Client connection → recv()/send()
	FD_CGI_PIPE		// CGI pipe → read() (Phase 3)
};

// --------------------------------------------------------------------------
// Client state — where this connection is in the request/response lifecycle
// --------------------------------------------------------------------------

enum ClientState {
	STATE_READING,		// Waiting for complete HTTP request
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
