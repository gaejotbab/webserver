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
#include <string.h>

static const int backlog = 32;

static const size_t initial_buf_capacity = 8;

static bool server_stopped = false;

struct socket_handler_data {
    int accepted_socket_fd;
};

static void *accepted_socket_handler(void *arg)
{
    struct socket_handler_data data = *((struct socket_handler_data *)arg);
    free(arg);

    printf("[%d] accepted_socket_handler\n", data.accepted_socket_fd);

    int buf_capacity = initial_buf_capacity;
    char *buf = malloc(buf_capacity);
    int buf_length = 0;

    int line_feed_index = -1;

    while (1) {
        printf("[%d] before recv\n", data.accepted_socket_fd);

        int received_length = recv(data.accepted_socket_fd,
                buf + buf_length, buf_capacity - buf_length, 0);

        if (received_length == -1) {
            perror("recv");
            exit(100);
        }

        printf("[%d] received_length=%d\n", data.accepted_socket_fd, received_length);

        line_feed_index = -1;
        for (int i = buf_length; i < buf_length + received_length; ++i) {
            if (buf[i] == '\n') {
                line_feed_index = i;
                break;
            }
        }

        if (line_feed_index != -1) {
            break;
        }

        buf_length += received_length;
        if (buf_length == buf_capacity) {
            buf_capacity *= 2;
            buf = realloc(buf, buf_capacity);
        }
    }

    // CR position
    buf[line_feed_index - 1] = '\0';

    printf("[%d] %s\n", data.accepted_socket_fd, buf);

    char *http_response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nHello, world!";
    send(data.accepted_socket_fd, http_response, strlen(http_response), 0);

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

        struct socket_handler_data *data = malloc(sizeof(struct socket_handler_data));
        data->accepted_socket_fd = accepted_socket_fd;

        pthread_t thread;

        int pthread_result = pthread_create(&thread, NULL, accepted_socket_handler, data);

        if (pthread_result != 0) {
            perror("pthread_create");
            exit(5);
        }

        printf("pthread %lu\n", thread);
    }

    return 0;
}
