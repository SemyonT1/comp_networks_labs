#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

int main() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080);
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");


    sendto(sockfd, "Hello from client", 17, 0, 
                    (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    char buffer[1024];
    recvfrom(sockfd, buffer, 1024, 0, NULL, NULL);
    std::cout << "Server: " << buffer << std::endl;
    close(sockfd);
    return 0;

}