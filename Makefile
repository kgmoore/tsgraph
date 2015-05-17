all: tsgraph

tsgraph: main.o
	g++ -lrt -g main.o -o tsgraph

main.o: main.cpp
	g++ -c -g -std=c++0x main.cpp

serve_http: serve_http.cpp
	gcc serve_http.cpp -o serve_http -lmicrohttpd

clean:
	rm -rf *.o tsgraph serve_http
