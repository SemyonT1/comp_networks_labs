#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdint.h>
#include <pthread.h>
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
    MSG_HELLO   = 1,
    MSG_WELCOME = 2,
    MSG_TEXT    = 3,
    MSG_PING    = 4,
    MSG_PONG    = 5,
    MSG_BYE     = 6
};

int sockfd = -1;
std::atomic<bool> connected = false;
pthread_mutex_t sock_mutex = PTHREAD_MUTEX_INITIALIZER;
char nick[MAX_PAYLOAD] = "user";

bool do_connect() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(PORT);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return false;
    }


    Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.type   = MSG_HELLO;
    msg.length = htonl(strlen(nick));
    strncpy(msg.payload, nick, MAX_PAYLOAD - 1);
    send(fd, &msg, sizeof(msg), 0);

    
    int bytes = recv(fd, &msg, sizeof(msg), 0);
    if (bytes <= 0 || msg.type != MSG_WELCOME) {
        close(fd);
        return false;
    }

    pthread_mutex_lock(&sock_mutex);
    sockfd    = fd;
    connected = true;
    pthread_mutex_unlock(&sock_mutex);

    std::cout << "\rConnected as '" << nick << "'" << std::endl;
    return true;
}
void* recv_thread(void*) {
    while (true) {
        if (!connected) {
            sleep(RECONNECT_DELAY);
            std::cout << "\rReconnecting..." << std::endl;
            if (do_connect())
                std::cout << "> " << std::flush;
            continue;
        }

        Message msg;
        int bytes = recv(sockfd, &msg, sizeof(msg), 0);

        if (bytes <= 0) {
            // сервер оборвал соединение
            std::cout << "\nServer disconnected. Retrying in "
                      << RECONNECT_DELAY << "s..." << std::endl;
            connected = false;
            pthread_mutex_lock(&sock_mutex);
            
            close(sockfd);
            sockfd = -1;
            pthread_mutex_unlock(&sock_mutex);
            continue;
        }

        msg.payload[MAX_PAYLOAD - 1] = '\0';

        switch (msg.type) {
            case MSG_TEXT:
                // \r перекрывает строку "> ", которую уже напечатал main
                std::cout << "\r" << msg.payload << "\n> " << std::flush;
                break;

            case MSG_PONG:
                std::cout << "\rPONG\n> " << std::flush;
                break;

            case MSG_BYE:
                std::cout << "\rDisconnected by server." << std::endl;
                connected = false;
                
                pthread_mutex_lock(&sock_mutex);
                
                close(sockfd);
                sockfd = -1;
                pthread_mutex_unlock(&sock_mutex);
                break;

            default:
                break;
        }
    }
    return nullptr;
}


int main() {
    std::cout << "Your nickname: ";
    std::cin >> nick;
    std::cin.ignore();

    // первое подключение
    while (!do_connect()) {
        std::cout << "Connection failed. Retrying in " << RECONNECT_DELAY << "s..." << std::endl;
        sleep(RECONNECT_DELAY);
    }

    // запускаем поток приёма
    pthread_t tid;
    pthread_create(&tid, nullptr, recv_thread, nullptr);
    pthread_detach(tid);

    Message msg;
    while (true) {
        std::cout << "> " << std::flush;
        std::string input;

        if (!std::getline(std::cin, input)) {
            // EOF (Ctrl+D)
            if (connected) {
                memset(&msg, 0, sizeof(msg));
                msg.type   = MSG_BYE;
                msg.length = htonl(0);
                send(sockfd, &msg, sizeof(msg), 0);
            }
            break;
        }

        if (!connected) {
            std::cout << "Not connected yet, please wait..." << std::endl;
            continue;
        }

        memset(&msg, 0, sizeof(msg));

        if (input == "/quit") {
            msg.type   = MSG_BYE;
            msg.length = htonl(0);
            send(sockfd, &msg, sizeof(msg), 0);
            break;

        } else if (input == "/ping") {
            msg.type   = MSG_PING;
            msg.length = htonl(0);
            send(sockfd, &msg, sizeof(msg), 0);

        } else if (!input.empty()) {
            msg.type   = MSG_TEXT;
            msg.length = htonl((uint32_t)input.size());
            strncpy(msg.payload, input.c_str(), MAX_PAYLOAD - 1);
            send(sockfd, &msg, sizeof(msg), 0);
        }
    }

    if (sockfd >= 0) close(sockfd);
    return 0;
}