#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdint.h>
#include <pthread.h>
#include <queue>

#define PORT            8080
#define MAX_PAYLOAD     1024
#define THREAD_POOL_SIZE 10
#define MAX_CLIENTS     100

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


typedef struct {
    int fd;
    char ip[INET_ADDRSTRLEN];
    uint16_t port;
    char nick[MAX_PAYLOAD];
} ClientInfo;


ClientInfo clients[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

std::queue<int> conn_queue;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  queue_cond  = PTHREAD_COND_INITIALIZER;


void add_client(int fd, const char* ip, uint16_t port, const char* nick) {
    pthread_mutex_lock(&clients_mutex);
    if (client_count < MAX_CLIENTS) {
        clients[client_count].fd = fd;
        clients[client_count].port = port;
        strncpy(clients[client_count].ip,   ip,   INET_ADDRSTRLEN - 1);
        strncpy(clients[client_count].nick, nick, MAX_PAYLOAD - 1);
        client_count++;
    }
    pthread_mutex_unlock(&clients_mutex);
}

void remove_client(int fd) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++) {
        if (clients[i].fd == fd) {
            std::cout << "Client disconnected: " << clients[i].nick
                      << " [" << clients[i].ip << ":" << clients[i].port << "]"
                      << std::endl;
            // заменяем удаляемого последним элементом
            clients[i] = clients[client_count - 1];
            client_count--;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void broadcast(Message* msg) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++) {
        send(clients[i].fd, msg, sizeof(Message), 0);
    }
    pthread_mutex_unlock(&clients_mutex);
}


void* worker_thread(void*) {
    while (true) {
        // ждём задание из очереди
        pthread_mutex_lock(&queue_mutex);
        while (conn_queue.empty())
            pthread_cond_wait(&queue_cond, &queue_mutex);
        int client_fd = conn_queue.front();
        conn_queue.pop();
        pthread_mutex_unlock(&queue_mutex);

        Message msg;


        int bytes = recv(client_fd, &msg, sizeof(msg), 0);
        if (bytes <= 0 || msg.type != MSG_HELLO) {
            close(client_fd);
            continue;
        }
        msg.payload[MAX_PAYLOAD - 1] = '\0';
        char nick[MAX_PAYLOAD];
        strncpy(nick, msg.payload, MAX_PAYLOAD - 1);

        // узнаём адрес клиента через дескриптор
        struct sockaddr_in peer;
        socklen_t peer_len = sizeof(peer);
        getpeername(client_fd, (struct sockaddr*)&peer, &peer_len);
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &peer.sin_addr, ip, INET_ADDRSTRLEN);
        uint16_t port = ntohs(peer.sin_port);

        std::cout << nick << " [" << ip << ":" << port << "] connected" << std::endl;

        memset(&msg, 0, sizeof(msg));
        msg.type   = MSG_WELCOME;
        msg.length = htonl(0);
        send(client_fd, &msg, sizeof(msg), 0);

     
        add_client(client_fd, ip, port, nick);

        bool active = true;
        while (active) {
            bytes = recv(client_fd, &msg, sizeof(msg), 0);

            // разрыв соединения
            if (bytes <= 0) {
                remove_client(client_fd);
                close(client_fd);
                break;
            }

            msg.payload[MAX_PAYLOAD - 1] = '\0';

            switch (msg.type) {

                case MSG_TEXT: {
                    
                    const int FULL_SIZE = 2 * MAX_PAYLOAD + INET_ADDRSTRLEN + 16;
                    char full[FULL_SIZE];

                    snprintf(full, FULL_SIZE, "%s [%s:%d]: %s",
                                nick, ip, port, msg.payload);

                    std::cout << full << std::endl;


                    memset(&msg, 0, sizeof(msg));
                    msg.type = MSG_TEXT;
                    msg.length = htonl(strlen(full));
                    strncpy(msg.payload, full, MAX_PAYLOAD - 1);
                    broadcast(&msg);     // рассылаем всем клиентам
                    break;
                }

                case MSG_PING: {
                    memset(&msg, 0, sizeof(msg));
                    msg.type   = MSG_PONG;
                    msg.length = htonl(0);
                    send(client_fd, &msg, sizeof(msg), 0);
                    break;
                }

                case MSG_BYE: {
                    remove_client(client_fd);
                    close(client_fd);
                    active = false;
                    break;
                }

                default:
                    std::cout << "Unknown message type: " << (int)msg.type << std::endl;
                    break;
            }
        }
    }
    return nullptr;
}


int main() {
    int server_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
        { perror("bind"); close(server_fd); return 1; }

    if (listen(server_fd, 10) < 0)
        { perror("listen"); close(server_fd); return 1; }

    std::cout << "Server started on port " << PORT << std::endl;

    
    pthread_t pool[THREAD_POOL_SIZE];
    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        pthread_create(&pool[i], nullptr, worker_thread, nullptr);
        pthread_detach(pool[i]);
    }
    std::cout << "Thread pool: " << THREAD_POOL_SIZE << " workers ready" << std::endl;

    
    while (true) {
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) { perror("accept"); continue; }

        pthread_mutex_lock(&queue_mutex);
        conn_queue.push(client_fd);
        pthread_cond_signal(&queue_cond);   // будим свободный поток
        pthread_mutex_unlock(&queue_mutex);
    }

    close(server_fd);
    return 0;
}