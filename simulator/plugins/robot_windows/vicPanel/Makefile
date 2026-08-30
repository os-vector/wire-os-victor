WEBOTS_HOME ?= ../../../../webots

libvicPanel.so: vicPanel.c
	gcc -shared -fPIC -Wall -O2 \
	  -I$(WEBOTS_HOME)/include/controller/c \
	  vicPanel.c -o libvicPanel.so \
	  -L$(WEBOTS_HOME)/lib/controller -lController

clean:
	rm -f libvicPanel.so
