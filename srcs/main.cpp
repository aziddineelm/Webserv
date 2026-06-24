#include "server/Server.hpp"
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <vector>

// --------------------------------------------------------------------------
// Global signal flag — checked by Server::run() loop
// --------------------------------------------------------------------------

volatile sig_atomic_t g_running = 1;

static void signalHandler(int signum) {
	(void)signum;
	g_running = 0;
}

void	setupSignals(){
	signal(SIGPIPE, SIG_IGN);          // Don't die on broken pipe (send to closed client)
	signal(SIGINT, signalHandler);     // Ctrl+C → graceful shutdown
	signal(SIGQUIT, signalHandler);    // Ctrl+\ → graceful shutdown
}

// --------------------------------------------------------------------------
// Main
// --------------------------------------------------------------------------

int main(int argc, char **argv) {
	(void)argc;
	(void)argv;

	// --- Signal setup ---
	setupSignals();


	std::vector<int> ports;
	ports.push_back(8080);
	ports.push_back(8081);

	// --- Initialize and run ---
	Server server;

	if (!server.init(ports)) {
		std::cerr << "[main] Server initialization failed" << std::endl;
		return 1;
	}

	server.run();

	// Destructor handles cleanup (RAII)
	return 0;
}
