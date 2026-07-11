# Cross-platform GNU Makefile for the Ride Booking System
# Outputs all binaries to build/

CXX ?= g++
BUILD_DIR ?= build

CXXFLAGS ?= -std=c++17 -Wall -Wextra
LDFLAGS ?=
LDLIBS ?=

ifeq ($(OS),Windows_NT)
  EXE := .exe
  LDLIBS += -lws2_32
  MKDIR_BUILD = if not exist "$(subst /,\,$(BUILD_DIR))" mkdir "$(subst /,\,$(BUILD_DIR))"
  RM_BUILD = if exist "$(subst /,\,$(BUILD_DIR))" rmdir /S /Q "$(subst /,\,$(BUILD_DIR))"
else
  EXE :=
  CXXFLAGS += -pthread
  LDLIBS += -pthread
  MKDIR_BUILD = mkdir -p "$(BUILD_DIR)"
  RM_BUILD = rm -rf "$(BUILD_DIR)"
endif

SERVER_SRC := src/server/main.cpp
DRIVER_CLIENT_SRC := src/clients/driver_client.cpp
PASSENGER_CLIENT_SRC := src/clients/passenger_client.cpp
RACE_DEMO_SRC := src/demos/race_demo.cpp

SERVER := $(BUILD_DIR)/ride_server$(EXE)
DRIVER_CLIENT := $(BUILD_DIR)/driver_client$(EXE)
PASSENGER_CLIENT := $(BUILD_DIR)/passenger_client$(EXE)
RACE_DEMO := $(BUILD_DIR)/race_demo$(EXE)

.PHONY: all server clients demos race-demo run clean help

all: $(SERVER) $(DRIVER_CLIENT) $(PASSENGER_CLIENT) $(RACE_DEMO)

server: $(SERVER)

clients: $(DRIVER_CLIENT) $(PASSENGER_CLIENT)

demos: $(RACE_DEMO)

$(BUILD_DIR):
	$(MKDIR_BUILD)

$(SERVER): $(SERVER_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

$(DRIVER_CLIENT): $(DRIVER_CLIENT_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

$(PASSENGER_CLIENT): $(PASSENGER_CLIENT_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

$(RACE_DEMO): $(RACE_DEMO_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

run: $(SERVER)
	$(SERVER)

race-demo: $(RACE_DEMO)
	$(RACE_DEMO) unsafe || true
	$(RACE_DEMO) safe

clean:
	$(RM_BUILD)

help:
	@echo "Targets:"
	@echo "  make all       Build server, clients, and demos into build/"
	@echo "  make server    Build only the backend server"
	@echo "  make clients   Build driver and passenger clients"
	@echo "  make demos     Build demo programs"
	@echo "  make race-demo Build and run unsafe/safe race demo"
	@echo "  make run       Build and run the backend server"
	@echo "  make clean     Remove build/"
	@echo ""
	@echo "Outputs:"
	@echo "  $(SERVER)"
	@echo "  $(DRIVER_CLIENT)"
	@echo "  $(PASSENGER_CLIENT)"
	@echo "  $(RACE_DEMO)"
