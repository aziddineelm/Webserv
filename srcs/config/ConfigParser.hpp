#ifndef CONFIGPARSER_HPP
#define CONFIGPARSER_HPP

#include "ServerConfig.hpp"
#include <exception>
#include <fstream>
#include <string>
#include <vector>

class ConfigParser {
private:
    std::vector<ServerConfig> _servers;
    std::string _configFilePath;
    std::vector<std::string> _tokens;

    void tokenize(std::ifstream& file);
    void parseServerBlock(std::vector<std::string>::iterator& it, const std::vector<std::string>::iterator& end);
    void parseLocationBlock(std::vector<std::string>::iterator& it, const std::vector<std::string>::iterator& end, ServerConfig& currentServer);

public:
    ConfigParser(const std::string& configFilePath);
    ~ConfigParser();

    void parse();
    void validate();

    std::vector<ServerConfig> getServers() const;

    // Custom exception class for parsing errors
    class ConfigException : public std::exception {
    private:
        std::string _msg;
    public:
        ConfigException(const std::string& msg);
        virtual ~ConfigException() throw();
        virtual const char* what() const throw();
    };
};

#endif