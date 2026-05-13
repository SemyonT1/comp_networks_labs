.PHONY: all clean


all: server client

server: server.cpp
	g++ server.cpp -Wall -Wextra -o server -pthread

client: client.cpp
	g++ client.cpp -Wall -Wextra -o client -pthread

clean:
	rm -rf *.o server client
