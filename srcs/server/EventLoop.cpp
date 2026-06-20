#include "EventLoop.hpp"
#include <iostream>
#include <cstring>		// strerror
#include <cerrno>		// errno
#include <unistd.h>		// close
#include <fcntl.h>		// fcntl, O_NONBLOCK
#include <netinet/in.h>	// sockaddr_in, ntohl, ntohs
#include <sys/socket.h>	// recv, send, accept
#include <csignal>		// sig_atomic_t

// External flag set by signal handler (defined in main.cpp)
extern volatile sig_atomic_t g_running;

#define POLL_TIMEOUT_MS		1000	// Wake up every second for timeout checks
#define CLIENT_TIMEOUT_SEC	60		// Close clients idle for 60 seconds
#define READ_BUFFER_SIZE	8192	// Stack buffer for recv() — one read per poll cycle

// --------------------------------------------------------------------------
// Constructor / Destructor
// --------------------------------------------------------------------------

EventLoop::EventLoop() : _running(false) {
}

EventLoop::~EventLoop() {
	// Close all tracked client FDs (listening FDs are owned by Socket/Server)
	for (std::map<int, Client>::iterator it = _clients.begin();
		 it != _clients.end(); ++it) {
		close(it->second.fd);
	}
}

// --------------------------------------------------------------------------
// Setup
// --------------------------------------------------------------------------

void EventLoop::addListenFd(int fd, int port) {
	_addPollFd(fd, POLLIN);
	_listenPorts[fd] = port;
	std::cout << "[EventLoop] Registered listener fd=" << fd
			  << " port=" << port << std::endl;
}

// --------------------------------------------------------------------------
// Main loop
// --------------------------------------------------------------------------

void EventLoop::run() {
	_running = true;
	std::cout << "[EventLoop] Running... (press Ctrl+C to stop)" << std::endl;

	while (_running && g_running) {
		int ready = poll(&_pollfds[0], _pollfds.size(), POLL_TIMEOUT_MS);

		if (ready < 0) {
			if (errno == EINTR)
				continue;	// Signal interrupted poll — just retry
			std::cerr << "[EventLoop] poll() error: "
					  << std::strerror(errno) << std::endl;
			break;
		}

		// Process events on ready FDs
		for (size_t i = 0; i < _pollfds.size() && ready > 0; ++i) {
			if (_pollfds[i].revents == 0)
				continue;
			--ready;

			int fd = _pollfds[i].fd;
			short revents = _pollfds[i].revents;

			// --- Listener FD: accept new connections ---
			if (_listenPorts.find(fd) != _listenPorts.end()) {
				if (revents & POLLIN)
					_handleAccept(fd);
				continue;
			}

			// --- Client FD: handle I/O events ---
			if (revents & (POLLERR | POLLNVAL)) {
				_handleDisconnect(fd);
				--i;	// Element swapped in from back — re-examine this index
				continue;
			}
			if (revents & POLLIN) {
				_handleRead(fd);
				if (_clients.find(fd) == _clients.end()) {
					--i;
					continue;	// Read detected disconnect
				}
			}
			if (revents & POLLOUT) {
				_handleWrite(fd);
				if (_clients.find(fd) == _clients.end()) {
					--i;
					continue;
				}
			}
			if (revents & POLLHUP) {
				_handleDisconnect(fd);
				--i;
			}
		}

		_checkTimeouts();
	}

	std::cout << "\n[EventLoop] Shutting down..." << std::endl;
}

void EventLoop::stop() {
	_running = false;
}

// --------------------------------------------------------------------------
// Event: Accept new client
// --------------------------------------------------------------------------

void EventLoop::_handleAccept(int listenFd) {
	int port = _listenPorts[listenFd];

	// Drain the accept queue — there may be multiple pending connections
	while (true) {
		struct sockaddr_in clientAddr;
		socklen_t addrLen = sizeof(clientAddr);

		int clientFd = accept(listenFd, (struct sockaddr *)&clientAddr, &addrLen);
		if (clientFd == -1) {
			// EAGAIN = no more pending connections — normal, we're done
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			std::cerr << "[EventLoop] accept() error on port " << port
					  << ": " << std::strerror(errno) << std::endl;
			break;
		}

		// Set non-blocking immediately
		if (fcntl(clientFd, F_SETFL, O_NONBLOCK) == -1) {
			std::cerr << "[EventLoop] fcntl(O_NONBLOCK) failed: "
					  << std::strerror(errno) << std::endl;
			close(clientFd);
			continue;
		}

		// Log connection (manual IP formatting)
		uint32_t ip = ntohl(clientAddr.sin_addr.s_addr);
		std::cout << "[EventLoop] Client connected on port " << port
				  << " from " << ((ip >> 24) & 0xFF) << "." << ((ip >> 16) & 0xFF) << "." << ((ip >> 8) & 0xFF) << "." << (ip & 0xFF)
				  << ":" << ntohs(clientAddr.sin_port) << " (fd " << clientFd << ")" << std::endl;

		// Register client: add to poll set (monitor for readable data)
		// and create Client entry with the Request parser
		_addPollFd(clientFd, POLLIN);
		_clients[clientFd] = Client(clientFd, port);
	}
}

// --------------------------------------------------------------------------
// Event: Read from client
// --------------------------------------------------------------------------

void EventLoop::_handleRead(int clientFd) {
	// Find the client — must exist if we got here
	std::map<int, Client>::iterator it = _clients.find(clientFd);
	if (it == _clients.end())
		return;
	Client &client = it->second;

	// Single recv per poll cycle — prevents one fast client from starving others
	char buf[READ_BUFFER_SIZE];
	ssize_t bytesRead = recv(clientFd, buf, sizeof(buf), 0);

	if (bytesRead == 0) {
		// Client closed connection cleanly (sent FIN)
		std::cout << "[EventLoop] Client disconnected (fd " << clientFd << ")" << std::endl;
		_handleDisconnect(clientFd);
		return;
	}
	if (bytesRead < 0) {
		std::cerr << "[EventLoop] recv() error on fd " << clientFd
				  << ": " << std::strerror(errno) << std::endl;
		_handleDisconnect(clientFd);
		return;
	}

	// Update activity timestamp
	client.lastActivity = time(NULL);

	// Direct-feed into Person B's streaming Request parser
	client.request.feed(std::string(buf, bytesRead));

	// Check if the parser has finished (complete request or parse error)
	if (client.request.isComplete() || client.request.hasError()) {
		client.state = STATE_WRITING;
		_setPollEvents(clientFd, POLLOUT);

		// Log what was parsed
		if (client.request.isComplete()) {
			std::cout << "[EventLoop] Request complete (fd " << clientFd << "): "
					  << client.request.getMethod() << " "
					  << client.request.getPath() << std::endl;
		} else {
			std::cerr << "[EventLoop] Request parse error (fd " << clientFd << "): "
					  << client.request.getErrorCode() << std::endl;
		}

		// STUB RESPONSE FOR TASK 3 (Buffer Setup)
		std::string responseBody = "<h1>Hello from Webserv!</h1>\n";
		client.writeBuffer = "HTTP/1.1 200 OK\r\n"
							 "Content-Type: text/html\r\n"
							 "Content-Length: 29\r\n" // Length of responseBody
							 "Connection: close\r\n\r\n"
							 + responseBody;

	}
}

// --------------------------------------------------------------------------
// Event: Write to client — STUB (Task 3)
// --------------------------------------------------------------------------

void EventLoop::_handleWrite(int clientFd) {
	std::map<int, Client>::iterator it = _clients.find(clientFd);
	if (it == _clients.end())
		return;
	Client &client = it->second;

	// Calculate how many bytes are left to send
	size_t remaining = client.writeBuffer.size() - client.writeOffset;
	
	// Call send() — passing the exact offset pointer
	// Note: We do NOT check errno after send() to comply with the 42 subject
	ssize_t bytesSent = send(clientFd, client.writeBuffer.data() + client.writeOffset, remaining, 0);

	if (bytesSent < 0) {
		std::cerr << "[EventLoop] send() error on fd " << clientFd
				  << ": " << std::strerror(errno) << std::endl;
		_handleDisconnect(clientFd);
		return;
	}

	// Update offset and activity timestamp
	client.writeOffset += bytesSent;
	client.lastActivity = time(NULL);

	// Check if the entire response has been sent
	if (client.writeOffset >= client.writeBuffer.size()) {
		std::cout << "[EventLoop] Response fully sent (fd " << clientFd << ")" << std::endl;
		
		// For Task 3, we implement "Connection: close" by simply disconnecting
		_handleDisconnect(clientFd);
	}
}

// --------------------------------------------------------------------------
// Event: Client disconnect — cleanup resources
// --------------------------------------------------------------------------

void EventLoop::_handleDisconnect(int clientFd) {
	close(clientFd);
	_removePollFd(clientFd);
	_clients.erase(clientFd);
}

// --------------------------------------------------------------------------
// Poll array helpers
// --------------------------------------------------------------------------

void EventLoop::_addPollFd(int fd, short events) {
	struct pollfd pfd;
	pfd.fd = fd;
	pfd.events = events;
	pfd.revents = 0;
	_pollfds.push_back(pfd);
}

void EventLoop::_removePollFd(int fd) {
	for (size_t i = 0; i < _pollfds.size(); ++i) {
		if (_pollfds[i].fd == fd) {
			// Swap with last element and pop — avoids O(n) shift
			_pollfds[i] = _pollfds.back();
			_pollfds.pop_back();
			return;
		}
	}
}

void EventLoop::_setPollEvents(int fd, short events) {
	for (size_t i = 0; i < _pollfds.size(); ++i) {
		if (_pollfds[i].fd == fd) {
			_pollfds[i].events = events;
			return;
		}
	}
}

// --------------------------------------------------------------------------
// Timeout check — STUB (Task 5)
// --------------------------------------------------------------------------

void EventLoop::_checkTimeouts() {
	// Task 5: scan _clients, close any with (now - lastActivity) > CLIENT_TIMEOUT_SEC
}
