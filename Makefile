.PHONY: all build server client run client-run demo clean data-clean distclean help

CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -I.
LDFLAGS := -pthread

SRC_DIR := src
INCLUDE_DIR := include
BUILD_DIR := build
BIN_DIR := bin
CLIENT_DIR := client
DATA_DIR := data

SOURCES := $(shell find $(SRC_DIR) -name "*.cpp")
MAIN_SOURCE := main.cpp
CLIENT_SOURCE := $(CLIENT_DIR)/cli_client.cpp

OBJECTS := $(SOURCES:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)
MAIN_OBJ := $(BUILD_DIR)/main.o

SERVER_BIN := $(BIN_DIR)/db_server
CLIENT_BIN := $(BIN_DIR)/db_client

all: build

server: $(SERVER_BIN)
	@echo "✓ Database server built successfully"

build: server

client: $(CLIENT_BIN)
	@echo "✓ Database client built successfully"

$(SERVER_BIN): $(OBJECTS) $(MAIN_OBJ) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(CLIENT_BIN): $(CLIENT_SOURCE) | $(BIN_DIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/main.o: $(MAIN_SOURCE) | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

run: build
	@./$(SERVER_BIN)

client-run: client
	@./$(CLIENT_BIN)

demo: build client
	@mkdir -p $(DATA_DIR) $(BUILD_DIR)
	@echo "CREATE TABLE demo_users (id INT PRIMARY KEY, name STRING NOT NULL);" > $(BUILD_DIR)/demo_queries.sql
	@echo "INSERT INTO demo_users VALUES (1, 'Alice');" >> $(BUILD_DIR)/demo_queries.sql
	@echo "SELECT * FROM demo_users;" >> $(BUILD_DIR)/demo_queries.sql
	@./$(SERVER_BIN) & \
	 DEMO_PID=$$!; \
	 sleep 1; \
	 ./$(CLIENT_BIN) $(BUILD_DIR)/demo_queries.sql; \
	 kill $$DEMO_PID 2>/dev/null; wait $$DEMO_PID 2>/dev/null || true
	@echo "✓ Demo finished"

clean:
	@rm -rf $(BUILD_DIR) $(BIN_DIR)
	@echo "✓ Build artifacts removed"

data-clean:
	@rm -rf $(DATA_DIR)
	@echo "✓ Data files removed"

distclean: clean data-clean

help:
	@echo "SQL Database Engine - Build System"
	@echo "===================================="
	@echo "Targets:"
	@echo "  make build       - Build server"
	@echo "  make client      - Build client"
	@echo "  make run         - Run server"
	@echo "  make demo        - Run quick server+client demo"
	@echo "  make clean       - Clean build artifacts"
	@echo "  make data-clean  - Clean data files"
	@echo "  make distclean   - Clean everything"
