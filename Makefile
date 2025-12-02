
# =====================================================
# MAKEFILE - ONLINE AUCTION SYSTEM
# =====================================================

CC = gcc
CFLAGS = -Wall -pthread
LDFLAGS = -lpthread

# Targets
SERVER = server
CLIENT = client

# Source files
SERVER_SRC = server.c
CLIENT_SRC = client.c

all: $(SERVER) $(CLIENT)

$(SERVER): $(SERVER_SRC)
	$(CC) $(CFLAGS) -o $(SERVER) $(SERVER_SRC) $(LDFLAGS)
	@echo "Server compiled successfully!"

$(CLIENT): $(CLIENT_SRC)
	$(CC) $(CFLAGS) -o $(CLIENT) $(CLIENT_SRC) $(LDFLAGS)
	@echo "Client compiled successfully!"

clean:
	rm -f $(SERVER) $(CLIENT)
	@echo "Cleaned build files"

clean-data:
	rm -rf data/
	@echo "Cleaned data files"

run-server: $(SERVER)
	./$(SERVER)

run-client: $(CLIENT)
	./$(CLIENT)

help:
	@echo "Available targets:"
	@echo "  make          - Compile both server and client"
	@echo "  make server   - Compile server only"
	@echo "  make client   - Compile client only"
	@echo "  make clean    - Remove compiled files"
	@echo "  make clean-data - Remove data directory"
	@echo "  make run-server - Run server"
	@echo "  make run-client - Run client"
