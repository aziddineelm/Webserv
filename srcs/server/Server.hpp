#ifndef SERVER_HPP
#define SERVER_HPP

#include "Socket.hpp"
#include <vector>

class Server {
public:
	Server();
	~Server();

	// Initialize: create listening sockets for each port
	bool	init(const std::vector<int> &ports);

	// Run the main loop (Phase 1: simple accept, Phase 2: poll)
	void	run();

	// Stop the server gracefully
	void	stop();

private:
	std::vector<Socket *>	_sockets;
	bool					_running;

	// Accept a new connection on a listening socket (Phase 1 helper)
	void	_acceptConnection(Socket &listenSocket);

	// Non-copyable
	Server(const Server &);
	Server &operator=(const Server &);
};

#endif
