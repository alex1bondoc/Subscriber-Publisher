CC=g++
CFLAGS=-std=c++17 -Wall -Wextra

all: build

build: server subscriber

server: server.o
	$(CC) $(CFLAGS) $^ -o $@

server.o: server.cpp 
	$(CC) $(CFLAGS) -c $<

subscriber:  subscriber.o
	$(CC) $(CFLAGS) $^ -o $@

subscriber.o: subscriber.cpp 
	$(CC) $(CFLAGS) -c $<


clean:
	rm -f *.o server subscriber