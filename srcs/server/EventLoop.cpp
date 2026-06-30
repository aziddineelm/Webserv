#include "EventLoop.hpp"
#include <iostream>
#include <cstring>		// strerror
#include <cerrno>		// errno
#include <unistd.h>		// close
#include <fcntl.h>		// fcntl, O_NONBLOCK
#include <netinet/in.h>	// sockaddr_in, ntohl, ntohs
#include <sys/socket.h>	// recv, send, accept
#include <csignal>		// sig_atomic_t
#include "../http/router/router.hpp"
#include "../http/response/response.hpp"

// External flag set by signal handler (defined in main.cpp)
extern volatile sig_atomic_t g_running;

#define POLL_TIMEOUT_MS		1000	// Wake up every second for timeout checks
#define CLIENT_TIMEOUT_SEC	60		// Close clients idle for 60 seconds
#define READ_BUFFER_SIZE	8192	// Stack buffer for recv() — one read per poll cycle

// --------------------------------------------------------------------------
// Constructor / Destructor
// --------------------------------------------------------------------------

EventLoop::EventLoop() : _running(false) {
	_epollFd = epoll_create(1024);
	if (_epollFd < 0) {
		std::cerr << "[EventLoop] epoll_create() error: " << std::strerror(errno) << std::endl;
	}
}

EventLoop::~EventLoop() {
	// Close all tracked client FDs (listening FDs are owned by Socket/Server)
	for (std::map<int, Client>::iterator it = _clients.begin();
		 it != _clients.end(); ++it) {
		close(it->second.fd);
	}
	if (_epollFd >= 0) {
		close(_epollFd);
	}
}

// --------------------------------------------------------------------------
// Setup
// --------------------------------------------------------------------------

void EventLoop::setConfigs(const std::vector<ServerConfig> &configs) {
	_configs = configs;
	std::cout << "[EventLoop] Loaded " << _configs.size() << " server configurations" << std::endl;
}

void EventLoop::addListenFd(int fd, int port) {
	_addEpollFd(fd, EPOLLIN);
	_listenPorts[fd] = port;
	std::cout << "[EventLoop] Registered listener fd=" << fd
			  << " port=" << port << std::endl;
}

// --------------------------------------------------------------------------
// Main loop
// --------------------------------------------------------------------------

void EventLoop::run() {
	if (_epollFd < 0) return;
	_running = true;
	std::cout << "[EventLoop] Running... (press Ctrl+C to stop)" << std::endl;

	const int MAX_EVENTS = 1024;
	struct epoll_event events[MAX_EVENTS];

	while (_running && g_running) {
		int numEvents = epoll_wait(_epollFd, events, MAX_EVENTS, POLL_TIMEOUT_MS);

		if (numEvents < 0) {
			if (errno == EINTR)
				continue;	// Signal interrupted epoll — just retry
			std::cerr << "[EventLoop] epoll_wait() error: "
					  << std::strerror(errno) << std::endl;
			break;
		}

		// Process active events
		for (int i = 0; i < numEvents; ++i) {
			int fd = events[i].data.fd;
			uint32_t revents = events[i].events;

			// --- Listener FD: accept new connections ---
			if (_listenPorts.find(fd) != _listenPorts.end()) {
				if (revents & EPOLLIN)
					_handleAccept(fd);
				continue;
			}

			// --- Client FD: handle I/O events ---
			if (revents & (EPOLLERR | EPOLLHUP)) {
				_handleDisconnect(fd);
				continue;
			}
			if (revents & EPOLLIN) {
				_handleRead(fd);
				if (_clients.find(fd) == _clients.end()) {
					continue;	// Read detected disconnect
				}
			}
			if (revents & EPOLLOUT) {
				_handleWrite(fd);
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

		// Register client: add to epoll set (monitor for readable data)
		// and create Client entry with the Request parser
		_addEpollFd(clientFd, EPOLLIN);
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
		_setEpollEvents(clientFd, EPOLLOUT);

		// Log what was parsed
		if (client.request.isComplete()) {
			std::cout << "[EventLoop] Request complete (fd " << clientFd << "): "
					  << client.request.getMethod() << " "
					  << client.request.getPath() << std::endl;
		} else {
			std::cerr << "[EventLoop] Request parse error (fd " << clientFd << "): "
					  << client.request.getErrorCode() << std::endl;
		}

		// --- BASIC SERVER MATCHING ---
		// (Note: Full Virtual Hosting by Host header will be done by Person B in Phase 5)
		const ServerConfig* bestConfig = NULL;
		for (size_t i = 0; i < _configs.size(); ++i) {
			for (size_t p = 0; p < _configs[i].listen_ports.size(); ++p) {
				if (_configs[i].listen_ports[p] == client.listenPort) {
					bestConfig = &_configs[i];
					break; // Pick the first config for this port
				}
			}
			if (bestConfig) break;
		}

		// --- ROUTING & RESPONSE BUILDING ---
		if (bestConfig) {
			Router router;
			router.handleRequest(client.request, client.response, bestConfig->getLocationList());
		} else {
			// Fallback if absolutely no config matches (shouldn't happen)
			client.response.buildErrorPage(500);
		}

		// Honor Keep-Alive request
		client.response.setHeader("Connection", client.request.isKeepAlive() ? "keep-alive" : "close");

		// Prime the pump: load the first chunk (HTTP headers) into the write buffer
		client.writeBuffer = client.response.getNextChunk();
		client.writeOffset = 0;

	}
}


// --------------------------------------------------------------------------
// Event: Write to client
// --------------------------------------------------------------------------

void EventLoop::_handleWrite(int clientFd) {
	std::map<int, Client>::iterator it = _clients.find(clientFd);
	if (it == _clients.end())
		return;
	Client &client = it->second;

	// Calculate how many bytes are left to send
	size_t remaining = client.writeBuffer.size() - client.writeOffset;
	
	// Call send() — passing the exact offset pointer
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

	// Check if the current buffer chunk has been fully sent
	if (client.writeOffset >= client.writeBuffer.size()) {
		// If there is more data to send (file streaming), load the next chunk
		if (!client.response.isDone()) {
			client.writeBuffer = client.response.getNextChunk();
			client.writeOffset = 0;
			return; // Wait for next POLLOUT cycle
		}

		std::cout << "[EventLoop] Response fully sent (fd " << clientFd << ")" << std::endl;
		
		if (client.request.isKeepAlive()) {
			std::cout << "[EventLoop] Keep-Alive: resetting connection for fd " << clientFd << std::endl;
			client.writeBuffer.clear();
			client.writeOffset = 0;
			client.request.reset();
			client.response = Response(); // Reset response for next request
			client.state = STATE_READING;
			client.lastActivity = time(NULL);
			_setEpollEvents(clientFd, EPOLLIN);
		} else {
			std::cout << "[EventLoop] Connection: close requested, disconnecting fd " << clientFd << std::endl;
			_handleDisconnect(clientFd);
		}
	}
}

// --------------------------------------------------------------------------
// Event: Client disconnect — cleanup resources
// --------------------------------------------------------------------------

void EventLoop::_handleDisconnect(int clientFd) {
	_removeEpollFd(clientFd);
	close(clientFd);
	_clients.erase(clientFd);
}


// --------------------------------------------------------------------------
// Poll array helpers
// --------------------------------------------------------------------------

void EventLoop::_addEpollFd(int fd, uint32_t events) {
	struct epoll_event ev;
	ev.events = events;
	ev.data.fd = fd;
	if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, fd, &ev) == -1) {
		std::cerr << "[EventLoop] epoll_ctl(EPOLL_CTL_ADD) failed for fd " << fd << ": " << std::strerror(errno) << std::endl;
	}
}

void EventLoop::_removeEpollFd(int fd) {
	if (epoll_ctl(_epollFd, EPOLL_CTL_DEL, fd, NULL) == -1) {
		std::cerr << "[EventLoop] epoll_ctl(EPOLL_CTL_DEL) failed for fd " << fd << ": " << std::strerror(errno) << std::endl;
	}
}

void EventLoop::_setEpollEvents(int fd, uint32_t events) {
	struct epoll_event ev;
	ev.events = events;
	ev.data.fd = fd;
	if (epoll_ctl(_epollFd, EPOLL_CTL_MOD, fd, &ev) == -1) {
		std::cerr << "[EventLoop] epoll_ctl(EPOLL_CTL_MOD) failed for fd " << fd << ": " << std::strerror(errno) << std::endl;
	}
}


// --------------------------------------------------------------------------
// Timeout check
// --------------------------------------------------------------------------

void EventLoop::_checkTimeouts() {
	time_t now = time(NULL);
	std::map<int, Client>::iterator it = _clients.begin();
	
	while (it != _clients.end()) {
		if (now - it->second.lastActivity > CLIENT_TIMEOUT_SEC) {
			std::cout << "[EventLoop] Client timeout (fd " << it->first << ") after " 
					  << CLIENT_TIMEOUT_SEC << " seconds of inactivity." << std::endl;
			int fdToClose = it->first;
			++it;
			_handleDisconnect(fdToClose);
		} else {
			++it;
		}
	}
}
