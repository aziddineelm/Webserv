#ifndef SERVER_HPP
#define SERVER_HPP

#include "Socket.hpp"
#include "EventLoop.hpp"
#include "../config/ServerConfig.hpp"
#include <vector>

class Server {
public:
	Server();
	~Server();

	// Initialize: create listening sockets for each port and pass configs to event loop
	bool	init(const std::vector<int> &ports, const std::vector<ServerConfig> &configs);

	// Run the event loop (delegates to EventLoop)
	void	run();

	// Stop the server gracefully
	void	stop();

private:
	std::vector<Socket *>	_sockets;
	EventLoop				_eventLoop;

	// Non-copyable
	Server(const Server &);
	Server &operator=(const Server &);
};

#endif
