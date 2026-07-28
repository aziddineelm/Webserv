#include "Server.hpp"
#include <iostream>

// ==========================================================================
// Constructor & Destructor
// ==========================================================================

Server::Server() {
}

Server::~Server() {
	for (size_t i = 0; i < _sockets.size(); ++i) {
		delete _sockets[i];
	}
	std::cout << "[Server] All sockets cleaned up" << std::endl;
}

// ==========================================================================
// Initialization
// ==========================================================================

bool Server::init(const std::vector<int> &ports, const std::vector<ServerConfig> &configs) {

	_eventLoop.setConfigs(configs);

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
			
			for (size_t j = 0; j < _sockets.size(); ++j)
				delete _sockets[j];
			_sockets.clear();
			
			return false;
		}
		_sockets.push_back(sock);
		_eventLoop.addListenFd(sock->getFd(), sock->getPort());
	}

	std::cout << "[Server] Initialized with " << _sockets.size()
			  << " listening socket(s)" << std::endl;

	return true;
}

// ==========================================================================
// Core Operations
// ==========================================================================

void Server::run() {
	_eventLoop.run();
}

void Server::stop() {
	_eventLoop.stop();
}
