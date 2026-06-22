GREEN 	= \033[1;32m
YELLOW 	= \033[1;33m
RED 	= \033[1;31m
RESET 	= \033[0m

CXX 		= c++

CXXFLAGS	= -Wall -Wextra -Werror -std=c++98

NAME 		= webserv

SRCS 		= srcs/main.cpp \
			  srcs/server/Socket.cpp \
			  srcs/server/Server.cpp \
			  srcs/server/EventLoop.cpp \
			  srcs/http/request/request.cpp

OBJS 		= $(SRCS:.cpp=.o)

CGI_TEST_NAME = cgi_test
CGI_TEST_SRCS = srcs/cgi/test_cgi_runner.cpp \
			 srcs/cgi/CGIHandler.cpp \
			 srcs/cgi/ProcessSpawner.cpp \
			 srcs/cgi/EnvBuilder.cpp \
			 srcs/http/CGIResponseParser.cpp

CGI_TEST_OBJS = $(CGI_TEST_SRCS:.cpp=.o)

all: $(NAME)

$(NAME): $(OBJS)
	@$(CXX) $(CXXFLAGS) -o $(NAME) $(OBJS)
	@echo "$(GREEN)$(NAME) compiled successfully!$(RESET)"

$(CGI_TEST_NAME): $(CGI_TEST_OBJS)
	@$(CXX) $(CXXFLAGS) -o $(CGI_TEST_NAME) $(CGI_TEST_OBJS)
	@echo "$(GREEN)$(CGI_TEST_NAME) compiled successfully!$(RESET)"

cgi-test: $(CGI_TEST_NAME)
	@./$(CGI_TEST_NAME)

%.o: %.cpp
	@$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	@rm -f $(OBJS) $(CGI_TEST_OBJS)
	@echo "$(YELLOW)Object files cleaned$(RESET)"

fclean: clean
	@rm -f $(NAME) $(CGI_TEST_NAME)
	@echo "$(RED)$(NAME) cleaned successfully!$(RESET)"

re: fclean all