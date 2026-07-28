#include "Socket.hpp"
#include <iostream>
#include <cstring>    // memset, strerror
#include <cerrno>     // errno
#include <unistd.h>   // close
#include <fcntl.h>    // fcntl, O_NONBLOCK
#include <sys/socket.h> // socket, AF_INET, SOCK_STREAM, SOL_SOCKET
#include <arpa/inet.h> // htonl, htons

// --------------------------------------------------------------------------
// Constructor / Destructor
// --------------------------------------------------------------------------

Socket::Socket() : _fd(-1), _port(0) {
	std::memset(&_addr, 0, sizeof(_addr));
}

Socket::~Socket() {
	if (_fd != -1) {
		close(_fd);
		std::cout << "[Socket] Closed listening socket on port "
				  << _port << " (fd " << _fd << ")" << std::endl;
	}
}

// --------------------------------------------------------------------------
// Public: setup
// --------------------------------------------------------------------------

bool Socket::setup(int port) {
	_port = port;

	if (!_createSocket())
		return false;
	if (!_setOptions())
		return false;
	if (!_bindSocket())
		return false;
	if (!_startListening())
		return false;
	if (!_setNonBlocking())
		return false;

	std::cout << "[Socket] Listening on port " << _port
			  << " (fd " << _fd << ")" << std::endl;
	return true;
}

// --------------------------------------------------------------------------
// Getters
// --------------------------------------------------------------------------

int Socket::getFd() const {
	return _fd;
}

int Socket::getPort() const {
	return _port;
}

// --------------------------------------------------------------------------
// Private helpers — one syscall each
// --------------------------------------------------------------------------

bool Socket::_createSocket() {
	_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (_fd == -1) {
		std::cerr << "[Socket] socket() failed: "
				  << std::strerror(errno) << std::endl;
		return false;
	}
	return true;
}

bool Socket::_setOptions() {
	int opt = 1;
	if (setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
		std::cerr << "[Socket] setsockopt(SO_REUSEADDR) failed: "
				  << std::strerror(errno) << std::endl;
		return false;
	}
	return true;
}

bool Socket::_bindSocket() {
	_addr.sin_family = AF_INET;
	_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	_addr.sin_port = htons(_port);

	if (bind(_fd, (struct sockaddr *)&_addr, sizeof(_addr)) == -1) {
		std::cerr << "[Socket] bind() failed on port " << _port << ": "
				  << std::strerror(errno) << std::endl;
		return false;
	}
	return true;
}

bool Socket::_startListening() {
	if (listen(_fd, 128) == -1) {
		std::cerr << "[Socket] listen() failed on port " << _port << ": "
				  << std::strerror(errno) << std::endl;
		return false;
	}
	return true;
}

bool Socket::_setNonBlocking() {
	if (fcntl(_fd, F_SETFL, O_NONBLOCK) == -1) {
		std::cerr << "[Socket] fcntl(O_NONBLOCK) failed on port " << _port << ": "
				  << std::strerror(errno) << std::endl;
		return false;
	}
	return true;
}
