all: tsgraph

tsgraph: main.o
	g++ -lrt -g main.o -o tsgraph

main.o: main.cpp
	g++ -c -g -std=c++0x main.cpp

clean:
	rm -rf *.o tsgraph
