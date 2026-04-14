#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <string>
#include <stdint.h>
#include <atomic>

#define PORT            8080
#define MAX_PAYLOAD     1024
#define RECONNECT_DELAY 2

typedef struct {
    uint32_t length;
    uint8_t  type;
    char     payload[MAX_PAYLOAD];
} Message;

enum {
    MSG_HELLO       = 1,
    MSG_WELCOME     = 2,
    MSG_TEXT        = 3,
    MSG_PING        = 4,
    MSG_PONG        = 5,
    MSG_BYE         = 6,
    MSG_AUTH        = 7,
    MSG_PRIVATE     = 8,
    MSG_ERROR       = 9,
    MSG_SERVER_INFO = 10
};

int               sockfd = -1;
std::atomic<bool> connected{false};     
pthread_mutex_t   sock_mutex = PTHREAD_MUTEX_INITIALIZER;
std::string       nickname;



void send_message(uint8_t type, const char* payload = nullptr) {
    Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = type;
    if (payload) {
        msg.length = htonl(strlen(payload));
        strncpy(msg.payload, payload, MAX_PAYLOAD - 1);
        msg.payload[MAX_PAYLOAD - 1] = '\0';
    } else {
        msg.length = htonl(0);
    }
    pthread_mutex_lock(&sock_mutex);
    if (connected && sockfd >= 0)
        send(sockfd, &msg, sizeof(msg), 0);
    pthread_mutex_unlock(&sock_mutex);
}



bool connect_to_server() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
        { close(fd); return false; }

    // HELLO
    Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.type   = MSG_HELLO;
    msg.length = htonl(0);
    if (send(fd, &msg, sizeof(msg), 0) < 0) { close(fd); return false; }

    // WELCOME
    int bytes = recv(fd, &msg, sizeof(msg), 0);
    if (bytes <= 0 || msg.type != MSG_WELCOME) { close(fd); return false; }

    // AUTH
    memset(&msg, 0, sizeof(msg));
    msg.type   = MSG_AUTH;
    msg.length = htonl(nickname.size());
    strncpy(msg.payload, nickname.c_str(), MAX_PAYLOAD - 1); 
    msg.payload[MAX_PAYLOAD - 1] = '\0';
    if (send(fd, &msg, sizeof(msg), 0) < 0) { close(fd); return false; }

    // ответ сервера
    bytes = recv(fd, &msg, sizeof(msg), 0);
    if (bytes <= 0) { close(fd); return false; }

    if (msg.type == MSG_ERROR) {
        msg.payload[MAX_PAYLOAD - 1] = '\0';
        std::cerr << "[ERROR] " << msg.payload << std::endl;
        close(fd);
        return false;
    }

    pthread_mutex_lock(&sock_mutex);
    sockfd    = fd;
    connected = true;
    pthread_mutex_unlock(&sock_mutex);

    std::cout << "Connected as " << nickname << std::endl;
    return true;
}



void* receive_thread(void*) {
    while (true) {
        if (!connected) {
            sleep(RECONNECT_DELAY);
            std::cout << "\rReconnecting..." << std::endl;
            if (connect_to_server())
                std::cout << "> " << std::flush;
            continue;
        }

        pthread_mutex_lock(&sock_mutex);
        int fd = sockfd;
        pthread_mutex_unlock(&sock_mutex);

        Message msg;
        int bytes = recv(fd, &msg, sizeof(msg), 0);

        if (bytes <= 0) {
            pthread_mutex_lock(&sock_mutex);
            connected = false;
            close(sockfd);
            sockfd = -1;
            pthread_mutex_unlock(&sock_mutex);
            std::cout << "\n[SYSTEM] Connection lost. Retrying in "
                      << RECONNECT_DELAY << "s..." << std::endl;
            continue;  
        }

        msg.payload[MAX_PAYLOAD - 1] = '\0';

        switch (msg.type) {
            case MSG_TEXT:
                std::cout << "\r" << msg.payload << "\n> " << std::flush;
                break;
            case MSG_PRIVATE:
                std::cout << "\r" << msg.payload << "\n> " << std::flush;
                break;
            case MSG_PONG:
                std::cout << "\r[PONG]\n> " << std::flush;
                break;
            case MSG_ERROR:
                std::cout << "\r[ERROR] " << msg.payload << "\n> " << std::flush;
                break;
            case MSG_SERVER_INFO:
                std::cout << "\r[SERVER] " << msg.payload << "\n> " << std::flush;
                break;
            case MSG_BYE:
                std::cout << "\r[SERVER] Disconnected." << std::endl;
                pthread_mutex_lock(&sock_mutex);
                connected = false;
                close(sockfd);
                sockfd = -1;
                pthread_mutex_unlock(&sock_mutex);
                return nullptr;
            default:
                break;
        }
    }
    return nullptr;
}



int main() {
    std::cout << "Enter your nickname: ";
    std::getline(std::cin, nickname);
    if (nickname.empty()) nickname = "Anonymous";

    if (!connect_to_server()) {
        std::cerr << "Failed to connect/authenticate. Exiting." << std::endl;
        return 1;
    }

    pthread_t tid;
    pthread_create(&tid, nullptr, receive_thread, nullptr);
    pthread_detach(tid);

    std::string input;
    while (true) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, input)) {
            send_message(MSG_BYE);
            break;
        }

        if (!connected) {
            std::cout << "Not connected, please wait..." << std::endl;
            continue;
        }

        if (input == "/quit") {
            send_message(MSG_BYE);
            break;
        } else if (input == "/ping") {
            send_message(MSG_PING);
        } else if (input.rfind("/w ", 0) == 0) {
            size_t space = input.find(' ', 3);
            if (space == std::string::npos) {
                std::cout << "Usage: /w <nick> <message>" << std::endl;
            } else {
                std::string target  = input.substr(3, space - 3);
                std::string message = input.substr(space + 1);
                send_message(MSG_PRIVATE, (target + ":" + message).c_str());
            }
        } else if (!input.empty()) {
            send_message(MSG_TEXT, input.c_str());
        }
    }

    pthread_mutex_lock(&sock_mutex);
    if (sockfd >= 0) close(sockfd);
    pthread_mutex_unlock(&sock_mutex);
    return 0;
}