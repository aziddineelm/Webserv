#include "server/Server.hpp"
#include "config/ConfigParser.hpp"
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
	// --- Signal setup ---
	setupSignals();

	// --- Config file path (default or from argv) ---
	std::string configPath = "config/default.conf";
	if (argc > 1)
		configPath = argv[1];

	// --- Parse configuration ---
	try {
		ConfigParser parser(configPath);
		parser.parse();
		parser.validate();

		std::cout << "[main] Configuration loaded from: " << configPath << std::endl;

		// Extract unique ports from all server blocks
		std::vector<int> ports = parser.getPorts();

		// --- Initialize and run ---
		Server server;

		if (!server.init(ports)) {
			std::cerr << "[main] Server initialization failed" << std::endl;
			return 1;
		}

		server.run();
	}
	catch (const ConfigParser::ConfigException &e) {
		std::cerr << "[main] Configuration error: " << e.what() << std::endl;
		return 1;
	}

	// Destructor handles cleanup (RAII)
	return 0;
}
