#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <string>
#include <ctime>
#include "../http/request/request.hpp"
#include "../http/response/response.hpp"
#include "../cgi/CGIHandler.hpp"


enum ClientState {
	STATE_READING,
	STATE_CGI_RUNNING,
	STATE_CGI_STREAMING,
	STATE_WRITING,
	STATE_DONE
};


struct Client {
	int				fd;
	ClientState		state;
	Request			request;
	Response		response;
	CGIHandler		cgi;
	std::string		writeBuffer;
	size_t			writeOffset;
	time_t			lastActivity;
	int				listenPort;

	Client()
		: fd(-1), state(STATE_READING),
		  writeOffset(0), lastActivity(0), listenPort(0) {}

	Client(int clientFd, int port)
		: fd(clientFd), state(STATE_READING),
		  writeOffset(0), lastActivity(time(NULL)), listenPort(port) {}
};

#endif
