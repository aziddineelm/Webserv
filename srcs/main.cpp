#include "server/Server.hpp"
#include "config/ConfigParser.hpp"
#include <iostream>
#include <csignal>
#include <cstdlib>

// ==========================================================================
// Signal Handling
// ==========================================================================

volatile sig_atomic_t g_running = 1;

static void signalHandler(int signum) {
	(void)signum;
	g_running = 0;
}

static void	setupSignals() {
	signal(SIGPIPE, SIG_IGN);          // Don't die on broken pipe (send to closed client)
	signal(SIGINT, signalHandler);     // Ctrl+C → graceful shutdown
	signal(SIGQUIT, signalHandler);    // Ctrl+\ → graceful shutdown
}

// ==========================================================================
// Main
// ==========================================================================

int main(int argc, char **argv) {

	setupSignals();

	if (argc > 2) {
		std::cerr << "Usage: " << argv[0] << " [config_file]" << std::endl;
		return 1;
	}
	
	std::string configPath = (argc == 2) ? argv[1] : "config/default.conf";

	try {
		ConfigParser parser(configPath);
		parser.parse();
		parser.validate();

		std::cout << "[main] Configuration loaded from: " << configPath << std::endl;

		Server server;
		if (!server.init(parser.getPorts(), parser.getServers())) {
			std::cerr << "[main] Server initialization failed" << std::endl;
			return 1;
		}

		server.run();
	}
	catch (const ConfigParser::ConfigException &e) {
		std::cerr << "[main] Configuration error: " << e.what() << std::endl;
		return 1;
	}
	catch (const std::exception &e) {
		std::cerr << "[main] Error: " << e.what() << std::endl;
		return 1;
	}

	return 0;
}
