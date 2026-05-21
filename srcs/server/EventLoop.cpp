#include "EventLoop.hpp"
#include <iostream>
#include <cstring>		// strerror
#include <cerrno>		// errno
#include <unistd.h>		// close
#include <fcntl.h>		// fcntl, O_NONBLOCK
#include <netinet/in.h>	// sockaddr_in, ntohl, ntohs
#include <csignal>		// sig_atomic_t

// External flag set by signal handler (defined in main.cpp)
extern volatile sig_atomic_t g_running;

#define POLL_TIMEOUT_MS		1000	// Wake up every second for timeout checks
#define CLIENT_TIMEOUT_SEC	60		// Close clients idle for 60 seconds

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
	_clients.clear();
	_pollfds.clear();
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
				continue;
			}
			if (revents & POLLIN)
				_handleRead(fd);
			if (revents & POLLOUT)
				_handleWrite(fd);
			if (revents & POLLHUP)
				_handleDisconnect(fd);
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

		// Log connection (manual IP formatting — inet_ntoa is forbidden)
		uint32_t ip = ntohl(clientAddr.sin_addr.s_addr);
		std::cout << "[EventLoop] Client connected on port " << port
				  << " from "
				  << ((ip >> 24) & 0xFF) << "."
				  << ((ip >> 16) & 0xFF) << "."
				  << ((ip >> 8) & 0xFF) << "."
				  << (ip & 0xFF)
				  << ":" << ntohs(clientAddr.sin_port)
				  << " (fd " << clientFd << ")" << std::endl;

		// TODO Task 2: Add to poll set and create Client entry
		// For Task 1: close immediately (no client tracking yet)
		close(clientFd);
	}
}

// --------------------------------------------------------------------------
// Event: Read from client — STUB (Task 2)
// --------------------------------------------------------------------------

void EventLoop::_handleRead(int clientFd) {
	(void)clientFd;
}

// --------------------------------------------------------------------------
// Event: Write to client — STUB (Task 3)
// --------------------------------------------------------------------------

void EventLoop::_handleWrite(int clientFd) {
	(void)clientFd;
}

// --------------------------------------------------------------------------
// Event: Client disconnect — STUB (Task 5)
// --------------------------------------------------------------------------

void EventLoop::_handleDisconnect(int clientFd) {
	(void)clientFd;
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
