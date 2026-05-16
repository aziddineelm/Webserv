// ...existing code...
#include "config/ConfigParser.hpp"
#include <iostream>

int main(int argc, char** argv) {
	std::string configPath = "config/default.conf";
	if (argc > 1) {
		configPath = argv[1];
	}

	try {
		ConfigParser parser(configPath);
		parser.parse();
		parser.validate();

		std::vector<ServerConfig> servers = parser.getServers();
		std::cout << "Parsed servers: " << servers.size() << std::endl;
		for (size_t i = 0; i < servers.size(); ++i) {
			std::cout << "\n[Server " << i + 1 << "]" << std::endl;
			servers[i].printConfig();
			std::cout << "  Locations: " << servers[i].locations.size() << std::endl;
			for (std::map<std::string, LocationContext>::const_iterator it = servers[i].locations.begin();
				 it != servers[i].locations.end(); ++it) {
				std::cout << "    - " << it->first << std::endl;
			}
		}
	} catch (const std::exception& ex) {
		std::cerr << "Config error: " << ex.what() << std::endl;
		return 1;
	}

	return 0;
}
