all: tsgraph

tsgraph: main.o
	g++ main.o -o tsgraph

main.o: main.cpp
	g++ -c main.cpp

clean:
	rm -rf *o tsgraph
