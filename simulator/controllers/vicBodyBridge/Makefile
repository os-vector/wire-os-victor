#WEBOTS_HOME=<repo>/webots make
#WEBOTS_HOME_WIN=<win-webots> make windows

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

UNAME_S := $(shell uname -s)

LIBDIRS = -L$(WEBOTS_HOME)/lib/controller -L$(WEBOTS_HOME)/lib/webots
ifeq ($(UNAME_S),Darwin)
  LIBS = -lCppController -lController -lpthread \
         -framework CoreFoundation -framework CoreAudio \
         -framework AudioUnit -framework AudioToolbox
  RPATH = -Wl,-rpath,$(WEBOTS_HOME)/lib/controller \
          -Wl,-rpath,$(WEBOTS_HOME)/lib/webots
else
  LIBS = -lCppController -lController -lpthread -ldl -lm
  RPATH = -Wl,-rpath,$(WEBOTS_HOME)/lib/controller \
          -Wl,-rpath,$(WEBOTS_HOME)/lib/webots \
          -Wl,-rpath-link,$(WEBOTS_HOME)/lib/webots
endif

SRCS = $(NAME).cpp
HDRS = $(REPO_ROOT)/simulator/bridge/vic_bridge_protocol.h \
       $(REPO_ROOT)/simulator/bridge/vic_face_protocol.h \
       $(REPO_ROOT)/simulator/bridge/vic_cam_protocol.h \
       $(REPO_ROOT)/simulator/bridge/vic_cube_protocol.h \
       $(REPO_ROOT)/simulator/bridge/vic_spkr_protocol.h \
       $(REPO_ROOT)/simulator/bridge/vic_net.h \
       miniaudio.h

$(NAME): $(SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRCS) -o $(NAME) $(LIBDIRS) $(LIBS) $(RPATH)

CXX_WIN ?= x86_64-w64-mingw32-g++
WEBOTS_HOME_WIN ?= $(REPO_ROOT)/webots-win

WIN_INCLUDES = \
  -I$(WEBOTS_HOME_WIN)/include/controller/cpp \
  -I$(WEBOTS_HOME_WIN)/include/controller/c \
  -I$(REPO_ROOT)/simulator/bridge \
  -I$(REPO_ROOT)/robot/syscon

WIN_LIBS = \
  $(WEBOTS_HOME_WIN)/lib/controller/CppController.dll \
  $(WEBOTS_HOME_WIN)/lib/controller/Controller.dll \
  -lws2_32

WIN_STATIC = -static-libgcc -static-libstdc++ \
  -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic

windows: $(NAME).exe

$(NAME).exe: $(SRCS) $(HDRS)
	$(CXX_WIN) $(CXXFLAGS) $(WIN_INCLUDES) $(SRCS) -o $(NAME).exe \
	  $(WIN_LIBS) $(WIN_STATIC)

clean:
	rm -f $(NAME) $(NAME).exe

.PHONY: clean windows
