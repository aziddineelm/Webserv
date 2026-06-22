#include "Server.hpp"
#include <iostream>

// --------------------------------------------------------------------------
// Constructor / Destructor
// --------------------------------------------------------------------------

Server::Server() {
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

		// Register with EventLoop for poll() monitoring
		_eventLoop.addListenFd(sock->getFd(), sock->getPort());
	}

	std::cout << "[Server] Initialized with " << _sockets.size()
			  << " listening socket(s)" << std::endl;
	return true;
}

// --------------------------------------------------------------------------
// Public: run — delegates to EventLoop
// --------------------------------------------------------------------------

void Server::run() {
	_eventLoop.run();
}

// --------------------------------------------------------------------------
// Public: stop — delegates to EventLoop
// --------------------------------------------------------------------------

void Server::stop() {
	_eventLoop.stop();
}
