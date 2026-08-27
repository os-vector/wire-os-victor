
#WEBOTS_HOME=<repo>/webots make

CONTROLLER_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
REPO_ROOT := $(abspath $(CONTROLLER_DIR)/../../..)
WEBOTS_HOME ?= $(REPO_ROOT)/webots

NAME = vicBodyBridge

CXX ?= g++
CXXFLAGS = -std=c++14 -Wall -O2

INCLUDES = \
  -I$(WEBOTS_HOME)/include/controller/cpp \
  -I$(WEBOTS_HOME)/include/controller/c \
  -I$(REPO_ROOT)/simulator/bridge \
  -I$(REPO_ROOT)/robot/syscon

LIBDIRS = -L$(WEBOTS_HOME)/lib/controller -L$(WEBOTS_HOME)/lib/webots
LIBS = -lCppController -lController -lasound -lpthread
RPATH = -Wl,-rpath,$(WEBOTS_HOME)/lib/controller \
        -Wl,-rpath,$(WEBOTS_HOME)/lib/webots \
        -Wl,-rpath-link,$(WEBOTS_HOME)/lib/webots

$(NAME): $(NAME).cpp $(REPO_ROOT)/simulator/bridge/vic_bridge_protocol.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(NAME).cpp -o $(NAME) $(LIBDIRS) $(LIBS) $(RPATH)

clean:
	rm -f $(NAME)

.PHONY: clean
