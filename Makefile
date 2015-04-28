CC=gcc
CXX=g++
RUN_FLAGS=-I. -O3 -std=c++0x $(CFLAGS) -I$(HOME)/boost_1_55_0 -lpcap
DBG_FLAGS=-I. -g  -std=c++0x $(CFLAGS) -I$(HOME)/boost_1_55_0 -lpcap

all: raw_controller

raw_controller:
	$(CXX) raw/RawController.cc -I./raw -lfluid_base $(RUN_FLAGS) -o raw_controller

raw_controller_debug:
	$(CXX) raw/RawController.cc -I./raw -lfluid_base $(DBG_FLAGS) -o raw_controller

clean:
	rm -f raw_controller
	rm -f *.gch

.PHONY : raw_controller clean
