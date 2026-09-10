# Instructor tooling. Unlike the competition binaries this one is allowed --
# and expected -- to use every core available.
CXX      ?= c++
CXXFLAGS ?= -O3 -std=c++17 -Wall -Wextra

# OpenMP is optional: without it refsolve still works, just serially.
OMPFLAG := $(shell printf 'int main(){return 0;}' > /tmp/.omp$$$$.cpp; \
             if $(CXX) -fopenmp -o /dev/null /tmp/.omp$$$$.cpp 2>/dev/null; \
             then echo -fopenmp; fi; rm -f /tmp/.omp$$$$.cpp)

.PHONY: all clean
all: refsolve

refsolve: refsolve.cpp
	$(CXX) $(CXXFLAGS) $(OMPFLAG) -o $@ $<
	@if [ -z "$(OMPFLAG)" ]; then \
	  echo "note: no OpenMP support detected -- refsolve will run single-threaded"; \
	fi

clean:
	rm -f $(SOLVERS)
	rm -f refsolve

# --- reference solvers ------------------------------------------------------
# These stand in for student submissions, so they follow the competition
# rules: no OpenMP, C++17, standard library only. Used by
# tools/discriminate.py to check that the dataset actually separates
# different levels of solution.
SOLVERS := solvers/bidij solvers/alt solvers/ch

.PHONY: solvers
solvers: $(SOLVERS)

solvers/%: solvers/%.cpp solvers/common.hpp
	$(CXX) $(CXXFLAGS) -o $@ $<
