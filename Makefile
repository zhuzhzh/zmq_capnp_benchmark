# 1. Settings
CXX := g++
CXXFLAGS := -std=c++17 -Wall -O2 -g

# Environment variables with defaults
ZEROMQ_HOME ?= /home/public/zeromq
CPPZMQ_HOME ?= /home/public/cppzmq
QUILL_HOME ?= /home/public/quill
CAPNPROTO_HOME ?= /home/public/capnproto

# Paths
BUILD_DIR := build
SRC_DIR := src

# Includes and Libs
INCLUDES := -I$(ZEROMQ_HOME)/include \
            -I$(CPPZMQ_HOME)/include \
            -I$(QUILL_HOME)/include \
            -I$(CAPNPROTO_HOME)/include \
            -I$(SRC_DIR) \
            -I$(BUILD_DIR)

LIBS := -L$(ZEROMQ_HOME)/lib -lzmq \
        -L$(CAPNPROTO_HOME)/lib -lcapnp -lkj \
        -lpthread -ldl -lrt

# 2. File Lists
CAPNP_SRC := $(SRC_DIR)/tlm_payload.capnp
CAPNP_GEN_H := $(BUILD_DIR)/src/tlm_payload.capnp.h
CAPNP_GEN_CXX := $(BUILD_DIR)/src/tlm_payload.capnp.c++
CAPNP_OBJ := $(BUILD_DIR)/src/tlm_payload.capnp.o

SERVER_SRC := $(SRC_DIR)/server.cpp
SERVER_OBJ := $(BUILD_DIR)/server.o
SERVER_EXE := $(BUILD_DIR)/server

CLIENT_SRC := $(SRC_DIR)/client.cpp
CLIENT_OBJ := $(BUILD_DIR)/client.o
CLIENT_EXE := $(BUILD_DIR)/client

# 3. Targets
.PHONY: all clean

all: $(SERVER_EXE) $(CLIENT_EXE)

# 4. Linking rules
$(SERVER_EXE): $(SERVER_OBJ) $(CAPNP_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LIBS)

$(CLIENT_EXE): $(CLIENT_OBJ) $(CAPNP_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LIBS)

# 5. Compilation rules
$(SERVER_OBJ): $(SERVER_SRC) $(CAPNP_GEN_H)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(CLIENT_OBJ): $(CLIENT_SRC) $(CAPNP_GEN_H)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(CAPNP_OBJ): $(CAPNP_GEN_CXX)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# 6. Codegen rule
$(CAPNP_GEN_H) $(CAPNP_GEN_CXX): $(CAPNP_SRC)
	@mkdir -p $(BUILD_DIR)/src
	capnp compile -I$(CAPNPROTO_HOME)/include -oc++:$(BUILD_DIR) $<

# 7. Clean rule
clean:
	rm -rf $(BUILD_DIR)