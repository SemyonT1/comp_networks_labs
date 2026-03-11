#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdint.h>

#define PORT 8080
#define MAX_PAYLOAD 1024

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

int main() {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    Message msg;

    // создание TCP сокета
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Ошибка создания сокета" << std::endl;
        return 1;
    }

    // разрешить переиспользование адреса после перезапуска
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // настройка адреса сервера
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(PORT);

    // привязка сокета
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Ошибка привязки" << std::endl;
        close(server_fd);
        return 1;
    }

    // ожидание подключения (одного клиента)
    if (listen(server_fd, 1) < 0) {
        std::cerr << "Ошибка listen" << std::endl;
        close(server_fd);
        return 1;
    }

    std::cout << "TCP сервер запущен на порту " << PORT << std::endl;
    std::cout << "Ожидание подключения..." << std::endl;

    // принятие клиента
    client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd < 0) {
        std::cerr << "Ошибка accept" << std::endl;
        close(server_fd);
        return 1;
    }

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    uint16_t client_port = ntohs(client_addr.sin_port);

    std::cout << "Client connected" << std::endl;

    // MSG_HELLO
    int bytes = recv(client_fd, &msg, sizeof(msg), 0);
    if (bytes <= 0 || msg.type != MSG_HELLO) {
        std::cerr << "Ошибка: ожидался HELLO" << std::endl;
        close(client_fd);
        close(server_fd);
        return 1;
    }

    msg.payload[MAX_PAYLOAD - 1] = '\0';
    std::cout << "[" << client_ip << ":" << client_port << "]: " << msg.payload << std::endl;

    // MSG_WELCOME
    memset(&msg, 0, sizeof(msg));
    msg.length = htonl(0);
    msg.type   = MSG_WELCOME;
    send(client_fd, &msg, sizeof(msg), 0);

    // цикл обработки сообщений
    while (true) {
        bytes = recv(client_fd, &msg, sizeof(msg), 0);

        if (bytes <= 0) {
            std::cout << "Client disconnected" << std::endl;
            break;
        }

        msg.payload[MAX_PAYLOAD - 1] = '\0';

        switch (msg.type) {
            case MSG_TEXT:
                std::cout << "[" << client_ip << ":" << client_port
                          << "]: " << msg.payload << std::endl;
                break;

            case MSG_PING:
                memset(&msg, 0, sizeof(msg));
                msg.type   = MSG_PONG;
                msg.length = htonl(0);
                send(client_fd, &msg, sizeof(msg), 0);
                break;

            case MSG_BYE:
                std::cout << "Client disconnected" << std::endl;
                close(client_fd);
                close(server_fd);
                return 0;

            default:
                std::cout << "Unknown type of message: " << (int)msg.type << std::endl;
                break;
        }
    }

    close(client_fd);
    close(server_fd);
    return 0;
}
