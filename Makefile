.PHONY: all build server client backup-tool run client-run demo demo-shard backup restore tests test clean data-clean distclean help

CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -Iinclude

ifeq ($(OS),Windows_NT)
  PLATFORM_DIR := win32
  LDFLAGS := -lws2_32
  CXXFLAGS += -D_WIN32_WINNT=0x0601
  DEMO_CMD = @./$(SERVER_BIN) & \
	DEMO_PID=$$!; \
	timeout /t 1 /nobreak >nul; \
	./$(CLIENT_BIN) $(BUILD_DIR)/demo_queries.sql; \
	taskkill //PID $$DEMO_PID //F >nul 2>&1; \
	wait $$DEMO_PID 2>/dev/null || true
else
  PLATFORM_DIR := posix
  LDFLAGS := -pthread
  DEMO_CMD = @./$(SERVER_BIN) & \
	DEMO_PID=$$!; \
	sleep 1; \
	./$(CLIENT_BIN) $(BUILD_DIR)/demo_queries.sql; \
	kill $$DEMO_PID 2>/dev/null; wait $$DEMO_PID 2>/dev/null || true
endif

SRC_DIR := src
INCLUDE_DIR := include
BUILD_DIR := build
BIN_DIR := bin
CLIENT_DIR := client
TOOLS_DIR := tools
DATA_DIR := data
BACKUP_DIR := backup
PLATFORM_DIR_PATH := platform/$(PLATFORM_DIR)

SOURCES := $(shell find $(SRC_DIR) -name "*.cc" ! -path "$(SRC_DIR)/platform/*")
PLATFORM_SOURCES := $(wildcard $(SRC_DIR)/$(PLATFORM_DIR_PATH)/*.cc)
COMMON_PLATFORM_SOURCES := $(SRC_DIR)/platform/tcp_client.cc

MAIN_SOURCE := main.cc
CLIENT_SOURCE := $(CLIENT_DIR)/cli_client.cc
BACKUP_SOURCE := $(TOOLS_DIR)/db_backup.cc

OBJECTS := $(SOURCES:$(SRC_DIR)/%.cc=$(BUILD_DIR)/%.o)
PLATFORM_OBJECTS := $(PLATFORM_SOURCES:$(SRC_DIR)/%.cc=$(BUILD_DIR)/%.o)
PLATFORM_OBJECTS += $(COMMON_PLATFORM_SOURCES:$(SRC_DIR)/%.cc=$(BUILD_DIR)/%.o)
MAIN_OBJ := $(BUILD_DIR)/main.o

SERVER_BIN := $(BIN_DIR)/nobugdb
CLIENT_BIN := $(BIN_DIR)/nobugdb-cli
BACKUP_BIN := $(BIN_DIR)/db_backup

# Google Test (git submodule: third_party/googletest)
GTEST_ROOT := third_party/googletest/googletest
GTEST_ALL_CC := $(GTEST_ROOT)/src/gtest-all.cc
GTEST_MAIN_CC := $(GTEST_ROOT)/src/gtest_main.cc
GTEST_CPPFLAGS := -isystem $(GTEST_ROOT)/include -I$(GTEST_ROOT)
GTEST_OBJECTS := $(BUILD_DIR)/third_party/gtest-all.o $(BUILD_DIR)/third_party/gtest_main.o

TEST_CPPFLAGS := -isystem $(GTEST_ROOT)/include -I.

TEST_DIR := tests
TEST_SOURCES := $(wildcard $(TEST_DIR)/*.cc)
TEST_OBJECTS := $(TEST_SOURCES:$(TEST_DIR)/%.cc=$(BUILD_DIR)/tests/%.o)

RUN_TESTS_BIN := $(BIN_DIR)/run_tests

CORE_OBJECTS := $(OBJECTS) $(PLATFORM_OBJECTS)
CLIENT_OBJECTS := $(BUILD_DIR)/$(PLATFORM_DIR_PATH)/tcp_socket.o

all: build

server: $(SERVER_BIN)
	@echo "✓ NoBugDB server built successfully ($(PLATFORM_DIR))"

build: server

client: $(CLIENT_BIN)
	@echo "✓ NoBugDB client built successfully ($(PLATFORM_DIR))"

backup-tool: $(BACKUP_BIN)
	@echo "✓ NoBugDB backup tool built successfully ($(PLATFORM_DIR))"

backup: backup-tool
	@mkdir -p $(BACKUP_DIR)
	./$(BACKUP_BIN) backup --data-dir $(DATA_DIR) --output-dir $(BACKUP_DIR)

restore: backup-tool
	./$(BACKUP_BIN) restore --backup-dir $(BACKUP_DIR) --data-dir $(DATA_DIR) --force

$(SERVER_BIN): $(CORE_OBJECTS) $(MAIN_OBJ) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(CLIENT_BIN): $(CLIENT_SOURCE) $(CLIENT_OBJECTS) | $(BIN_DIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(BACKUP_BIN): $(BACKUP_SOURCE) $(CORE_OBJECTS) | $(BIN_DIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/main.o: $(MAIN_SOURCE)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

$(BUILD_DIR)/third_party/gtest-all.o: $(GTEST_ALL_CC)
	@mkdir -p $(dir $@)
	@if [ ! -f "$(GTEST_ALL_CC)" ]; then \
	  echo "Google Test not found. Run: git submodule update --init --recursive"; \
	  exit 1; \
	fi
	$(CXX) $(CXXFLAGS) $(GTEST_CPPFLAGS) -c $(GTEST_ALL_CC) -o $@

$(BUILD_DIR)/third_party/gtest_main.o: $(GTEST_MAIN_CC)
	@mkdir -p $(dir $@)
	@if [ ! -f "$(GTEST_MAIN_CC)" ]; then \
	  echo "Google Test not found. Run: git submodule update --init --recursive"; \
	  exit 1; \
	fi
	$(CXX) $(CXXFLAGS) $(GTEST_CPPFLAGS) -c $(GTEST_MAIN_CC) -o $@

$(BUILD_DIR)/tests/%.o: $(TEST_DIR)/%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(TEST_CPPFLAGS) -c $< -o $@

$(RUN_TESTS_BIN): $(CORE_OBJECTS) $(TEST_OBJECTS) $(GTEST_OBJECTS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

tests: $(RUN_TESTS_BIN)
	@echo "✓ Tests binary built successfully"

test: tests
	@./$(RUN_TESTS_BIN)

run: build
	@./$(SERVER_BIN)

client-run: client
	@./$(CLIENT_BIN)

demo: build client
	@mkdir -p $(DATA_DIR) $(BUILD_DIR)
	@echo "CREATE TABLE demo_users (id INT PRIMARY KEY, name STRING NOT NULL);" > $(BUILD_DIR)/demo_queries.sql
	@echo "INSERT INTO demo_users VALUES (1, 'Alice');" >> $(BUILD_DIR)/demo_queries.sql
	@echo "SELECT * FROM demo_users;" >> $(BUILD_DIR)/demo_queries.sql
	$(DEMO_CMD)
	@echo "✓ Demo finished"

demo-shard: build
	@chmod +x scripts/demo_sharded_cluster.sh
	@./scripts/demo_sharded_cluster.sh

clean:
	@rm -rf $(BUILD_DIR) $(BIN_DIR)
	@echo "✓ Build artifacts removed"

data-clean:
	@rm -rf $(DATA_DIR)
	@echo "✓ Data files removed"

distclean: clean data-clean

help:
	@echo "NoBugDB - Build System"
	@echo "===================================="
	@echo "Platform backend: $(PLATFORM_DIR)"
	@echo "Targets:"
	@echo "  make build       - Build server"
	@echo "  make client      - Build client"
	@echo "  make backup-tool - Build offline backup/restore CLI (bin/db_backup)"
	@echo "  make backup      - Offline backup of DATA_DIR into BACKUP_DIR (stop server first)"
	@echo "  make restore     - Restore BACKUP_DIR into DATA_DIR (--force)"
	@echo "  make run         - Run server"
	@echo "  make demo        - Run quick server+client demo"
	@echo "  make demo-shard  - Run 2-worker + coordinator sharding demo"
	@echo "  make tests       - Build unit/integration tests (requires submodule googletest)"
	@echo "  make test        - Build and run tests"
	@echo "  make clean       - Clean build artifacts"
	@echo "  make data-clean  - Clean data files"
	@echo "  make distclean   - Clean everything"
