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
			  srcs/server/EventLoop.cpp

OBJS 		= $(SRCS:.cpp=.o)

all: $(NAME)

$(NAME): $(OBJS)
	@$(CXX) $(CXXFLAGS) -o $(NAME) $(OBJS)
	@echo "$(GREEN)$(NAME) compiled successfully!$(RESET)"

%.o: %.cpp
	@$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	@rm -f $(OBJS)
	@echo "$(YELLOW)Object files cleaned$(RESET)"

fclean: clean
	@rm -f $(NAME)
	@echo "$(RED)$(NAME) cleaned successfully!$(RESET)"

re: fclean all