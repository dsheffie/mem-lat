OBJ = mem_micro.o traverse.o loaded.o
CXX = g++
EXE = mem_micro
OPT = -O2
CXXFLAGS = -std=c++11 -g $(OPT) -pthread
LIBS = -pthread
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
