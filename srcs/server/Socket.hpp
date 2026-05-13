#ifndef SOCKET_HPP
#define SOCKET_HPP

#include <netinet/in.h>

class Socket {
public:
	Socket();
	~Socket();

	// Setup: creates, binds, and listens on the given port
	// Returns true on success, false on failure (logs the error)
	bool	setup(int port);

	// Getters
	int		getFd() const;
	int		getPort() const;

private:
	int					_fd;
	int					_port;
	struct sockaddr_in	_addr;

	// Internal helpers — each wraps one syscall
	bool	_createSocket();
	bool	_setOptions();
	bool	_bindSocket();
	bool	_startListening();
	bool	_setNonBlocking();

	// Non-copyable (prevent double close)
	Socket(const Socket &);
	Socket &operator=(const Socket &);
};

#endif
