CONTROLLER_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
REPO_ROOT := $(abspath $(CONTROLLER_DIR)/../../..)

NAME = vicCozmoBridge

CC ?= gcc
CXX ?= g++
CXXFLAGS = -std=c++14 -Wall -Wextra -Werror -O2
CFLAGS = -Wall -O2

MINIAUDIO_DIR = $(REPO_ROOT)/simulator/controllers/vicBodyBridge

INCLUDES = \
  -I$(REPO_ROOT)/simulator/bridge \
  -I$(REPO_ROOT)/robot/syscon \
  -I$(MINIAUDIO_DIR)

LIBS = -lpthread -ldl -lm

SRCS = cozmo_link.cpp cozmo_robot.cpp cozmo_body.cpp cozmo_face.cpp cozmo_audio.cpp cozmo_camera.cpp
HDRS = cozmo_link.h cozmo_robot.h cozmo_body.h cozmo_face.h cozmo_audio.h cozmo_camera.h \
       $(REPO_ROOT)/simulator/bridge/vic_net.h \
       $(REPO_ROOT)/simulator/bridge/vic_bridge_protocol.h

all: $(NAME)

stb_image_impl.o: stb_image_impl.c stb_image.h
	$(CC) $(CFLAGS) -Wno-unused-function -c stb_image_impl.c -o stb_image_impl.o

miniaudio_impl.o: miniaudio_impl.c $(MINIAUDIO_DIR)/miniaudio.h
	$(CC) $(CFLAGS) -I$(MINIAUDIO_DIR) -c miniaudio_impl.c -o miniaudio_impl.o

$(NAME): $(NAME).cpp $(SRCS) $(HDRS) miniaudio_impl.o stb_image_impl.o
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(NAME).cpp $(SRCS) miniaudio_impl.o stb_image_impl.o -o $(NAME) $(LIBS)

clean:
	rm -f $(NAME) miniaudio_impl.o stb_image_impl.o

.PHONY: all clean
