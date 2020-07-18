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
#include <stdarg.h>

void log_debug(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vprintf(format, ap);
    va_end(ap);
}

static const int backlog = 32;

static const size_t initial_str_buf_capacity = 64;
static const size_t recv_buf_capacity = 2048;

static bool server_stopped = false;

struct SocketHandlerData {
    int fd;
};

struct RecvBuffer {
    int fd;

    char *buf;
    int pos;
    int len;
    int cap;
};

char *recv_str_until(struct RecvBuffer *recv_buffer, char c) {
    int str_buf_capacity = initial_str_buf_capacity;
    char *str_buf = malloc(str_buf_capacity);
    int str_buf_len = 0;

    while (1) {
        if (recv_buffer->pos == recv_buffer->len) {
            recv_buffer->pos = 0;
            recv_buffer->len = recv(recv_buffer->fd, recv_buffer->buf, recv_buffer->cap, 0);

            if (recv_buffer->len == -1) {
                perror("recv");
                exit(100);
            }
        }

        int index = -1;
        for (int i = recv_buffer->pos; i < recv_buffer->len; ++i) {
            if (recv_buffer->buf[i] == c) {
                index = i;
                break;
            }
        }

        if (index == -1) {
            while (str_buf_len + recv_buffer->len > str_buf_capacity) {
                str_buf_capacity *= 2;
                str_buf = realloc(str_buf, str_buf_capacity);
            }

            memcpy(str_buf + str_buf_len, recv_buffer->buf, recv_buffer->cap);
            str_buf_len += recv_buffer->len;

            recv_buffer->pos = recv_buffer->len;
        } else {
            while (str_buf_len + (index + 1 - recv_buffer->pos) > str_buf_capacity) {
                str_buf_capacity *= 2;
                str_buf = realloc(str_buf, str_buf_capacity);
            }

            memcpy(str_buf + str_buf_len, recv_buffer->buf + recv_buffer->pos,
                    (index + 1 - recv_buffer->pos));
            str_buf[str_buf_len + (index + 1 - recv_buffer->pos)] = '\0';
            str_buf_len += (index + 1 - recv_buffer->pos);

            recv_buffer->pos = index + 1;

            break;
        }
    }

    return str_buf;
}

char *recv_line(struct RecvBuffer *recv_buffer)
{
    return recv_str_until(recv_buffer, '\n');
}

void remove_crlf(char *str)
{
    int len = strlen(str);

    if (str[len - 1] == '\n') {
        str[len - 1] = '\0';
    }

    if (str[len - 2] == '\r') {
        str[len - 2] = '\0';
    }
}

static void *accepted_socket_handler(void *arg)
{
    struct SocketHandlerData data = *((struct SocketHandlerData *)arg);
    free(arg);

    int fd = data.fd;

    log_debug("[%d] thread started: fn=accepted_socket_handler\n", fd);

    struct RecvBuffer recv_buffer = {
        .fd = fd,

        .buf = malloc(recv_buf_capacity),
        .pos = 0,
        .len = 0,
        .cap = recv_buf_capacity,
    };

    char *request_line = recv_line(&recv_buffer);
    remove_crlf(request_line);

    log_debug("[%d] first line: %s\n", fd, request_line);

    char *method;
    char *request_target;
    char *http_version;

    char *first_space_ptr = strchr(request_line, ' ');
    *first_space_ptr = '\0';

    method = strdup(request_line);

    char *second_space_ptr = strchr(first_space_ptr + 1, ' ');
    *second_space_ptr = '\0';

    request_target = strdup(first_space_ptr + 1);

    http_version = strdup(second_space_ptr + 1);

    log_debug("[%d] Request line\n\tMethod: %s\n\tRequest target: %s\n\tHTTP version: %s\n",
            fd, method, request_target, http_version);

    char *first_header = recv_line(&recv_buffer);
    remove_crlf(first_header);

    log_debug("[%d] first header: %s\n", fd, first_header);

    char *http_response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nHello, world!";

    int send_result = send(fd, http_response, strlen(http_response), 0);
    if (send_result == -1) {
        perror("send");
    }

    close(fd);

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

    while (!server_stopped) {
        int accepted_socket_fd = accept(socket_fd,
                (struct sockaddr *)&accepted_socket_addr, &accepted_socket_length);

        if (accepted_socket_fd == -1) {
            perror("accept");
            exit(4);
        }

        log_debug("accepted: fd=%d, addr=%s:%d\n",
                accepted_socket_fd,
                inet_ntoa(accepted_socket_addr.sin_addr),
                accepted_socket_addr.sin_port);

        struct SocketHandlerData *data = malloc(sizeof(struct SocketHandlerData));
        data->fd = accepted_socket_fd;

        /*
        pthread_t thread;

        int pthread_result = pthread_create(&thread, NULL, accepted_socket_handler, data);

        if (pthread_result != 0) {
            perror("pthread_create");
            exit(5);
        }
        */
        accepted_socket_handler(data);
    }

    return 0;
}
