all: tsgraph

tsgraph: main.o
	g++ -g main.o -o tsgraph

main.o: main.cpp
	g++ -c -g -std=c++11 main.cpp

clean:
	rm -rf *.o tsgraph
