#include "Server.hpp"
#include <iostream>
#include <cstring>     // strerror
#include <cerrno>      // errno
#include <unistd.h>    // close, usleep
#include <netinet/in.h> // htonl, htons
#include <fcntl.h>     // fcntl
#include <csignal>     // sig_atomic_t

// External flag set by signal handler (defined in main.cpp)
extern volatile sig_atomic_t g_running;

// --------------------------------------------------------------------------
// Constructor / Destructor
// --------------------------------------------------------------------------

Server::Server() : _running(false) {
}

Server::~Server() {
	for (size_t i = 0; i < _sockets.size(); ++i) {
		delete _sockets[i];
	}
	std::cout << "[Server] All sockets cleaned up" << std::endl;
}

// --------------------------------------------------------------------------
// Public: init
// --------------------------------------------------------------------------

bool Server::init(const std::vector<int> &ports) {
	if (ports.empty()) {
		std::cerr << "[Server] Error: no ports to listen on" << std::endl;
		return false;
	}

	for (size_t i = 0; i < ports.size(); ++i) {
		Socket *sock = new Socket();
		if (!sock->setup(ports[i])) {
			std::cerr << "[Server] Failed to set up socket on port "
					  << ports[i] << std::endl;
			delete sock;
			// Clean up already created sockets
			for (size_t j = 0; j < _sockets.size(); ++j)
				delete _sockets[j];
			_sockets.clear();
			return false;
		}
		_sockets.push_back(sock);
	}

	std::cout << "[Server] Initialized with " << _sockets.size()
			  << " listening socket(s)" << std::endl;
	return true;
}

// --------------------------------------------------------------------------
// Public: run — Phase 1 simple accept loop
// --------------------------------------------------------------------------

void Server::run() {
	_running = true;
	std::cout << "[Server] Running... (press Ctrl+C to stop)" << std::endl;

	while (_running && g_running) {
		// Try to accept on each listening socket
		for (size_t i = 0; i < _sockets.size(); ++i) {
			_acceptConnection(*_sockets[i]);
		}
		// Prevent busy-spinning (temporary — replaced by poll() in Phase 2)
		usleep(100000); // 100ms
	}

	std::cout << "\n[Server] Shutting down..." << std::endl;
}

// --------------------------------------------------------------------------
// Public: stop
// --------------------------------------------------------------------------

void Server::stop() {
	_running = false;
}

// --------------------------------------------------------------------------
// Private: _acceptConnection
// --------------------------------------------------------------------------

void Server::_acceptConnection(Socket &listenSocket) {
	struct sockaddr_in clientAddr;
	socklen_t addrLen = sizeof(clientAddr);

	int clientFd = accept(listenSocket.getFd(),
						  (struct sockaddr *)&clientAddr, &addrLen);

	if (clientFd == -1) {
		// Non-blocking: EAGAIN/EWOULDBLOCK means no pending connection — normal
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return;
		// Any other error is unexpected
		std::cerr << "[Server] accept() error on port "
				  << listenSocket.getPort() << ": "
				  << std::strerror(errno) << std::endl;
		return;
	}

	// Log the new connection
	uint32_t ip = ntohl(clientAddr.sin_addr.s_addr);
	std::cout << "[Server] New client connected on port "
			  << listenSocket.getPort()
			  << " from " << ((ip >> 24) & 0xFF) << "." << ((ip >> 16) & 0xFF) << "." << ((ip >> 8) & 0xFF) << "." << (ip & 0xFF)
			  << ":" << ntohs(clientAddr.sin_port)
			  << " (fd " << clientFd << ")" << std::endl;

	// Phase 1: we can't do anything with the client yet, so close it
	// Phase 2: we'll set non-blocking, add to poll set, and track it
	// TODO: Phase 2 — fcntl(clientFd, F_SETFL, O_NONBLOCK) + add to poll set
	close(clientFd);
}
