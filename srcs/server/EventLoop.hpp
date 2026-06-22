#ifndef EVENTLOOP_HPP
#define EVENTLOOP_HPP

#include "Client.hpp"
#include <vector>
#include <map>
#include <poll.h>

class EventLoop {
public:
	EventLoop();
	~EventLoop();

	// Setup — register listening sockets before run()
	void	addListenFd(int fd, int port);

	// Main loop — blocks until stop() or signal
	void	run();
	void	stop();

private:
	std::vector<struct pollfd>	_pollfds;
	std::map<int, Client>		_clients;		// clientFd → Client
	std::map<int, int>			_listenPorts;	// listenFd → port
	bool						_running;

	// Event handlers
	void	_handleAccept(int listenFd);
	void	_handleRead(int clientFd);
	void	_handleWrite(int clientFd);
	void	_handleDisconnect(int clientFd);

	// Poll array management
	void	_addPollFd(int fd, short events);
	void	_removePollFd(int fd);
	void	_setPollEvents(int fd, short events);

	// Timeout scanning
	void	_checkTimeouts();

	// Non-copyable
	EventLoop(const EventLoop &);
	EventLoop &operator=(const EventLoop &);
};

#endif
