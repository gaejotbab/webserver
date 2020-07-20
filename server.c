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

struct HandleClientArgs {
    int sk;
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
                return NULL;
            }
        }

        int index = -1;
        for (int i = recv_buffer->pos; i < recv_buffer->len; ++i) {
            if (recv_buffer->buf[i] == c) {
                index = i;
                break;
            }
        }

        int n = (index == -1 ? recv_buffer->len : index + 1) - recv_buffer->pos;

        while (str_buf_len + n > str_buf_capacity) {
            str_buf_capacity *= 2;
            str_buf = realloc(str_buf, str_buf_capacity);
        }

        memcpy(str_buf + str_buf_len, recv_buffer->buf + recv_buffer->pos, n);
        str_buf_len += (index + 1 - recv_buffer->pos);

        recv_buffer->pos += n;

        if (index != -1) {
            break;
        }
    }

    if (str_buf_len + 1 > str_buf_capacity) {
		++str_buf_capacity;
		str_buf = realloc(str_buf, str_buf_capacity);
	}

    str_buf[str_buf_len] = '\0';

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

static void *handle_client(void *arg)
{
    struct HandleClientArgs data = *((struct HandleClientArgs *)arg);
    free(arg);

    int fd = data.sk;

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
    int server_sk = socket(AF_INET, SOCK_STREAM, 0);

    if (server_sk == -1) {
        perror("socket");
        exit(1);
    }

    int so_reuseaddr_enabled = 1;
    if (setsockopt(server_sk, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr_enabled, sizeof(int)) == -1) {
        perror("setsockopt");
        exit(1);
    }

    struct sockaddr_in bind_addr;
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(8080);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server_sk, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) == -1) {
        perror("bind");
        exit(2);
    }

    if (listen(server_sk, backlog) == -1) {
        perror("listen");
        exit(3);
    }

    while (!server_stopped) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_length = sizeof(client_addr);

        int sk = accept(server_sk,
                (struct sockaddr *)&client_addr, &client_addr_length);

        if (sk == -1) {
            perror("accept");
            exit(4);
        }

        struct HandleClientArgs *args = malloc(sizeof(struct HandleClientArgs));
        args->sk = sk;

        pthread_t client_thread;

        int pthread_result = pthread_create(&client_thread, NULL, handle_client, args);

        if (pthread_result != 0) {
            perror("pthread_create");
            exit(5);
        }
    }

    return 0;
}
