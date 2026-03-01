#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define BUF_SIZE 1024

int main() {

	int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
	sockaddr_in serverAddr, clientAddr;
	serverAddr.sin_port = htons(8080);
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = INADDR_ANY;
	
	bind(sock_fd, (struct sockaddr*)&serverAddr, sizeof(serverAddr));

	char buffer[BUF_SIZE];
	socklen_t addrLen = sizeof(clientAddr);
	int n = recvfrom(sock_fd, buffer, 1024, 0, 
				(struct sockaddr*)&clientAddr, &addrLen);
	buffer[n] = '\n';
	char ip[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));
	int port = ntohs(clientAddr.sin_port);
	std::cout << "Client = " << ip << ":" << port << " -> " << buffer << std::endl;
	sendto(sock_fd, "hello from server", 17, 0, 
			(struct sockaddr*)&clientAddr, addrLen);
	close(sock_fd);
	return 0;
	

}
