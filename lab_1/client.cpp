#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <cstring>
int main() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080);
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    char buffer[1024];
    while (true) {
        std::cout << "Enter message: ";
        std::cin.getline(buffer, 1024);

        if (strcmp(buffer, "enough") == 0)
            break;

        
        sendto(sockfd, buffer, strlen(buffer), 0,
               (struct sockaddr*)&serverAddr, sizeof(serverAddr));

       
        int n = recvfrom(sockfd, buffer, 1024 - 1, 0, NULL, NULL);

        if (n < 0) {
            perror("recvfrom error");
            continue;
        }

        buffer[n] = '\0';

        std::cout << "Server: " << buffer << std::endl;
    }

    return 0;

}