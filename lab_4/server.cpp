#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <vector>
#include <queue>
#include <algorithm>
#include <stdint.h>
#include <string>
#include <atomic>

#define PORT 8080
#define MAX_PAYLOAD 1024
#define THREAD_POOL_SIZE 10

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

struct ClientInfo {
    int                sockfd;
    struct sockaddr_in addr;
    std::string        nickname;
    bool               authenticated;
};

std::vector<ClientInfo> clients;
std::queue<int>         client_queue;
pthread_mutex_t         clients_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t         queue_mutex   = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t          queue_cond    = PTHREAD_COND_INITIALIZER;
std::atomic<bool>       server_running{true};  

// OSI

void log_osi(int layer, const char* action) {
    const char* name;
    switch (layer) {
        case 4:  name = "Transport";    break;
        case 5:  name = "Session";      break;
        case 6:  name = "Presentation"; break;
        case 7:  name = "Application";  break;
        default: name = "Unknown";
    }
    std::cout << "[Layer " << layer << " - " << name << "] " << action << std::endl;
}




void send_message(int sockfd, uint8_t type, const char* payload = nullptr) {
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
    log_osi(7, "prepare response");
    log_osi(6, "serialize Message");
    log_osi(4, "send()");
    send(sockfd, &msg, sizeof(msg), 0);
}


void broadcast_message(const std::string& text, int exclude_sock) {
    Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.type   = MSG_TEXT;
    msg.length = htonl(text.size());
    strncpy(msg.payload, text.c_str(), MAX_PAYLOAD - 1);

    pthread_mutex_lock(&clients_mutex);
    for (const auto& c : clients)
        if (c.sockfd != exclude_sock && c.authenticated)
            send(c.sockfd, &msg, sizeof(msg), 0);
    pthread_mutex_unlock(&clients_mutex);
}

void broadcast_server_info(const std::string& text, int exclude_sock) {
    Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.type   = MSG_SERVER_INFO;
    msg.length = htonl(text.size());
    strncpy(msg.payload, text.c_str(), MAX_PAYLOAD - 1);

    pthread_mutex_lock(&clients_mutex);
    for (const auto& c : clients)
        if (c.sockfd != exclude_sock && c.authenticated)
            send(c.sockfd, &msg, sizeof(msg), 0);
    pthread_mutex_unlock(&clients_mutex);
}

void remove_client(int sockfd) {
    pthread_mutex_lock(&clients_mutex);
    auto it = std::find_if(clients.begin(), clients.end(),
        [sockfd](const ClientInfo& c) { return c.sockfd == sockfd; });
    if (it != clients.end()) {
        std::cout << "User [" << it->nickname << "] disconnected" << std::endl;
        clients.erase(it);
    }
    pthread_mutex_unlock(&clients_mutex);
}


void* worker_thread(void*) {
    while (server_running) {
        pthread_mutex_lock(&queue_mutex);
        while (client_queue.empty() && server_running)
            pthread_cond_wait(&queue_cond, &queue_mutex);
        if (!server_running) {
            pthread_mutex_unlock(&queue_mutex);
            break;
        }
        int client_fd = client_queue.front();
        client_queue.pop();
        pthread_mutex_unlock(&queue_mutex);

        // адрес клиента
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        getpeername(client_fd, (struct sockaddr*)&client_addr, &addr_len);
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        int client_port = ntohs(client_addr.sin_port);

        Message msg;

        log_osi(4, "recv()");
        int bytes = recv(client_fd, &msg, sizeof(msg), 0);
        if (bytes <= 0 || msg.type != MSG_HELLO) {
            log_osi(6, "deserialize Message");
            std::cerr << "Expected HELLO, closing" << std::endl;
            close(client_fd);
            continue;
        }
        log_osi(6, "deserialize Message type=HELLO");
        send_message(client_fd, MSG_WELCOME);

        bool authenticated = false;
        std::string nickname;

        log_osi(4, "recv()");
        bytes = recv(client_fd, &msg, sizeof(msg), 0);
        if (bytes <= 0) { close(client_fd); continue; }

        log_osi(6, "deserialize Message");

        if (msg.type != MSG_AUTH) {
            log_osi(5, "session: expected MSG_AUTH");
            send_message(client_fd, MSG_ERROR, "Authentication required");
            close(client_fd);
            continue;
        }

        msg.payload[MAX_PAYLOAD - 1] = '\0';
        nickname = std::string(msg.payload);

        if (nickname.empty()) {
            log_osi(5, "session: empty nickname");
            send_message(client_fd, MSG_ERROR, "Nickname cannot be empty");
            close(client_fd);
            continue;
        }

        // проверка уникальности
        bool taken = false;
        pthread_mutex_lock(&clients_mutex);
        for (const auto& c : clients) {
            if (c.nickname == nickname) { taken = true; break; }
        }
        pthread_mutex_unlock(&clients_mutex);

        if (taken) {
            log_osi(5, ("session: nick taken: " + nickname).c_str());
            send_message(client_fd, MSG_ERROR, ("Nickname already taken: " + nickname).c_str());
            close(client_fd);
            continue;
        }

        authenticated = true;
        log_osi(5, ("authentication success: " + nickname).c_str());

        // добавляем клиента
        ClientInfo new_client{client_fd, client_addr, nickname, true};
        pthread_mutex_lock(&clients_mutex);
        clients.push_back(new_client);
        pthread_mutex_unlock(&clients_mutex);

        std::cout << "User [" << nickname << "] connected from "
                  << client_ip << ":" << client_port << std::endl;

        // уведомляем остальных о подключении
        send_message(client_fd, MSG_SERVER_INFO, ("Welcome, " + nickname + "!").c_str());
        broadcast_server_info("User [" + nickname + "] connected", client_fd);

        // цикл сообщений 
        bool active = true;
        while (active && server_running) {
            log_osi(4, "recv()");
            bytes = recv(client_fd, &msg, sizeof(msg), 0);
            if (bytes <= 0) break;

            log_osi(6, "deserialize Message");
            msg.payload[MAX_PAYLOAD - 1] = '\0';

            switch (msg.type) {

                case MSG_TEXT: {
                    log_osi(7, "handle MSG_TEXT");
                    std::string formatted = "[" + nickname + "]: " + msg.payload;
                    std::cout << formatted << std::endl;
                    broadcast_message(formatted, client_fd);
                    break;
                }

                case MSG_PRIVATE: {
                    log_osi(7, "handle MSG_PRIVATE");
                    std::string payload_str(msg.payload);
                    size_t colon = payload_str.find(':');
                    if (colon == std::string::npos) {
                        send_message(client_fd, MSG_ERROR, "Bad format. Use: /w <nick> <message>");
                        break;
                    }
                    std::string target  = payload_str.substr(0, colon);
                    std::string text    = payload_str.substr(colon + 1);

                    if (target == nickname) {
                        send_message(client_fd, MSG_ERROR, "Cannot send private message to yourself");
                        break;
                    }

                    std::string priv_msg = "[PRIVATE][" + nickname + "]: " + text;
                    std::string confirm  = "[PRIVATE → " + target + "]: " + text;

                    pthread_mutex_lock(&clients_mutex);
                    bool found = false;
                    for (const auto& c : clients) {
                        if (c.nickname == target && c.authenticated) {
                            // fix 5: send напрямую внутри лока, не через send_message
                            Message out;
                            memset(&out, 0, sizeof(out));
                            out.type   = MSG_PRIVATE;
                            out.length = htonl(priv_msg.size());
                            strncpy(out.payload, priv_msg.c_str(), MAX_PAYLOAD - 1);
                            send(c.sockfd, &out, sizeof(out), 0);
                            found = true;
                            break;
                        }
                    }
                    pthread_mutex_unlock(&clients_mutex);

                    if (!found) {
                        send_message(client_fd, MSG_ERROR, ("User not found: " + target).c_str());
                    } else {
                        send_message(client_fd, MSG_PRIVATE, confirm.c_str());
                    }
                    break;
                }

                case MSG_PING:
                    log_osi(7, "handle MSG_PING");
                    send_message(client_fd, MSG_PONG);
                    break;

                case MSG_BYE:
                    log_osi(7, "handle MSG_BYE");
                    active = false;
                    break;

                default:
                    log_osi(7, "unknown message type");
                    send_message(client_fd, MSG_ERROR, "Unknown command");
                    break;
            }
        }

        // уведомляем остальных об отключении
        remove_client(client_fd);
        broadcast_server_info("User [" + nickname + "] disconnected", client_fd);
        close(client_fd);
    }
    return nullptr;
}



int main() {
    int server_fd;
    struct sockaddr_in server_addr;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }

  
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
        { perror("bind"); close(server_fd); return 1; }

    if (listen(server_fd, 10) < 0)
        { perror("listen"); close(server_fd); return 1; }

    std::cout << "Server listening on port " << PORT << std::endl;

    pthread_t threads[THREAD_POOL_SIZE];
    for (int i = 0; i < THREAD_POOL_SIZE; i++)
        pthread_create(&threads[i], nullptr, worker_thread, nullptr);

    while (server_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) { perror("accept"); continue; }

        pthread_mutex_lock(&queue_mutex);
        client_queue.push(client_fd);
        pthread_cond_signal(&queue_cond);
        pthread_mutex_unlock(&queue_mutex);
    }

    server_running = false;
    pthread_cond_broadcast(&queue_cond);
    for (int i = 0; i < THREAD_POOL_SIZE; i++)
        pthread_join(threads[i], nullptr);
    close(server_fd);
    return 0;
}