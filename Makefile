OBJ = mem_micro.o traverse.o loaded.o
UNAME := $(shell uname -s)
ifeq ($(UNAME),Darwin)
CXX = clang++
OBJ += m1cycles.o placement.o
LIBS = -pthread -framework IOKit -framework CoreFoundation
else
CXX = clang++-20 #g++
LIBS = -pthread
endif
EXE = mem_micro
OPT = -O2
CXXFLAGS = -std=c++11 -g $(OPT) -pthread
DEP = $(OBJ:.o=.d)

.PHONY: all clean

all: $(EXE)

$(EXE) : $(OBJ)
	$(CXX) $(CXXFLAGS) $(OBJ) $(LIBS) -o $(EXE)

%.o: %.cc
	$(CXX) -MMD $(CXXFLAGS) -c $< 

-include $(DEP)

clean:
	rm -f $(EXE) $(OBJ) $(DEP) *.csv *~
