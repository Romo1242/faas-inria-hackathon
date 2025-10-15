CC=gcc
CFLAGS=-Wall -Wextra -O2 -std=c11 -D_GNU_SOURCE -DUSE_WASMER -I/usr/local/include
LDFLAGS=-L/usr/local/lib
WASMER_LIBS=-lwasmer -ldl -Wl,-rpath,/usr/local/lib

PREFIX?=/usr/local
BUILD_DIR=build
SRC_DIR=src
INC_DIR=include
BIN_DIR=$(BUILD_DIR)/bin
OBJ_DIR=$(BUILD_DIR)/obj

BINS=$(BIN_DIR)/server $(BIN_DIR)/worker $(BIN_DIR)/load_injector

COMMON_OBJS=$(OBJ_DIR)/ipc.o $(OBJ_DIR)/storage.o
SERVER_OBJS=$(OBJ_DIR)/main_server.o $(OBJ_DIR)/api_gateway.o $(OBJ_DIR)/load_balancer.o $(OBJ_DIR)/server.o $(COMMON_OBJS)

all: dirs $(BINS)

# Unified server binary (includes API Gateway, Load Balancer, and Server)
$(BIN_DIR)/server: $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(WASMER_LIBS) -lpthread

$(BIN_DIR)/worker: $(OBJ_DIR)/worker.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(WASMER_LIBS)

$(BIN_DIR)/load_injector: $(OBJ_DIR)/load_injector.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lpthread

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | dirs
	$(CC) $(CFLAGS) -I$(INC_DIR) -c $< -o $@

.PHONY: dirs clean distclean run

dirs:
	@mkdir -p $(BIN_DIR) $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR)

distclean: clean
	rm -rf $(BUILD_DIR)

run: all
	@echo "Run components individually. See README.md"
