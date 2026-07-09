#ifndef EVENTLOOP_HPP
#define EVENTLOOP_HPP

#include "Client.hpp"
#include "../config/ServerConfig.hpp"

#include <vector>
#include <map>
#include <sys/epoll.h>

class EventLoop {
public:
	EventLoop();
	~EventLoop();

	// Setup — register listening sockets before run()
	void	setConfigs(const std::vector<ServerConfig> &configs);
	void	addListenFd(int fd, int port);

	// Main loop — blocks until stop() or signal
	void	run();
	void	stop();

private:
	int							_epollFd;
	std::map<int, Client>		_clients;		// clientFd → Client
	std::map<int, int>			_listenPorts;	// listenFd → port
	std::map<int, int>			_cgiToClient;	// cgiFd → clientFd
	std::vector<ServerConfig>	_configs;		// All server configurations
	bool						_running;

	// Event handlers
	void	_handleAccept(int listenFd);
	void	_handleRead(int clientFd);
	void	_handleWrite(int clientFd);
	void	_handleDisconnect(int clientFd);
	void	_handleCgiReady(int pipeFd, uint32_t events);

	// Request dispatching & CGI helpers
	void	_dispatchRequest(int clientFd, Client &client);
	void	_spawnCgi(int clientFd, Client &client, const ServerConfig &bestConfig);
	void	_startWriting(Client &client);

	// epoll management
	void	_addEpollFd(int fd, uint32_t events);
	void	_removeEpollFd(int fd);
	void	_setEpollEvents(int fd, uint32_t events);

	// Timeout scanning
	void	_checkTimeouts();

	// Non-copyable
	EventLoop(const EventLoop &);
	EventLoop &operator=(const EventLoop &);
};

#endif
