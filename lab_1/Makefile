.PHONY: all clean

all: client server

server: server.cpp
	g++ server.cpp -Wall -Wextra -o server
client: client.cpp
	g++ client.cpp -Wall -Wextra -o client

clean:
	rm -rf *.o client server