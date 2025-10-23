# Awale Game Server - Makefile

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g
LDFLAGS = 

# Directories
COMMON_DIR = common
GAME_DIR = game
SERVER_DIR = server
CLIENT_DIR = client

# Object files
COMMON_OBJS = $(COMMON_DIR)/net.o $(COMMON_DIR)/protocol.o
GAME_OBJS = $(GAME_DIR)/awale.o
SERVER_OBJS = $(SERVER_DIR)/server.o $(SERVER_DIR)/session.o
CLIENT_OBJS = $(CLIENT_DIR)/client.o

# Executables
SERVER_BIN = awale_server
CLIENT_BIN = awale_client
TEST_BIN = test_awale

# Default target
all: $(SERVER_BIN) $(CLIENT_BIN)

# Server executable
$(SERVER_BIN): $(COMMON_OBJS) $(GAME_OBJS) $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Server built successfully: $(SERVER_BIN)"

# Client executable
$(CLIENT_BIN): $(COMMON_OBJS) $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Client built successfully: $(CLIENT_BIN)"

# Test executable (optional)
test: $(COMMON_OBJS) $(GAME_OBJS) tests/test_awale.o
	$(CC) $(CFLAGS) -o $(TEST_BIN) $^ $(LDFLAGS)
	@echo "Test built successfully: $(TEST_BIN)"

# Compile .c to .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -f $(COMMON_DIR)/*.o
	rm -f $(GAME_DIR)/*.o
	rm -f $(SERVER_DIR)/*.o
	rm -f $(CLIENT_DIR)/*.o
	rm -f tests/*.o
	rm -f $(SERVER_BIN) $(CLIENT_BIN) $(TEST_BIN)
	rm -f test_awale test_game
	rm -f *.o *.awl
	@echo "Cleaned build artifacts"

# Run server
run-server: $(SERVER_BIN)
	./$(SERVER_BIN)

# Run client
run-client: $(CLIENT_BIN)
	./$(CLIENT_BIN)

# Rebuild everything
rebuild: clean all

.PHONY: all clean test run-server run-client rebuild
