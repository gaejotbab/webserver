#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <pthread.h>

static const int backlog = 32;

static const size_t buf_len = 1024;

static bool server_stopped = false;

struct socket_handler_data {
    int accepted_socket_fd;
};

static void *accepted_socket_handler(void *arg)
{
    struct socket_handler_data data = *((struct socket_handler_data *)arg);

    char buf[buf_len];

    int received_length = recv(data.accepted_socket_fd, buf, buf_len, 0);
    buf[received_length] = '\0';

    printf("[%d] %s\n", data.accepted_socket_fd, buf);

    close(data.accepted_socket_fd);

    return NULL;
}

int main(int argc, char **argv)
{
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (socket_fd == -1) {
        perror("socket");
        exit(1);
    }

    int so_reuseaddr_enabled = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr_enabled, sizeof(int)) == -1) {
        perror("setsockopt");
    }

    struct sockaddr_in bind_addr;
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(8080);
    bind_addr.sin_addr.s_addr = 0;

    int bind_result = bind(socket_fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr));

    if (bind_result == -1) {
        perror("bind");
        exit(2);
    }

    int listen_result = listen(socket_fd, backlog);

    if (listen_result == -1) {
        perror("listen");
        exit(3);
    }

    struct sockaddr_in accepted_socket_addr;
    socklen_t accepted_socket_length = sizeof(accepted_socket_addr);

    int count = 0;

    while (!server_stopped) {
        printf("count: %d\n", count++);

        int accepted_socket_fd = accept(socket_fd,
                (struct sockaddr *)&accepted_socket_addr, &accepted_socket_length);

        if (accepted_socket_fd == -1) {
            perror("accept");
            exit(4);
        }

        printf("accepted: return=%d\n", accepted_socket_fd);

        struct socket_handler_data data;
        data.accepted_socket_fd = accepted_socket_fd;

        pthread_t thread;

        int pthread_result = pthread_create(&thread, NULL, accepted_socket_handler, &data);

        if (pthread_result != 0) {
            perror("pthread_create");
            exit(5);
        }
    }

    return 0;
}
