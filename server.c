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

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

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

enum HttpMethod {
    HTTP_METHOD_GET = 1,
    HTTP_METHOD_HEAD,
    HTTP_METHOD_POST,
    HTTP_METHOD_PUT,
    HTTP_METHOD_DELETE,
    HTTP_METHOD_TRACE,
    HTTP_METHOD_OPTIONS,
    HTTP_METHOD_CONNECT,
    HTTP_METHOD_PATCH,
};

enum HttpVersion {
    HTTP_VERSION_0_9 = 1,
    HTTP_VERSION_1_0,
    HTTP_VERSION_1_1,
};

struct HandleClientArgs {
    int sk;

    struct sockaddr_in client_addr;
};

struct RecvBuffer {
    int fd;

    char *buf;
    int pos;
    int len;
    int cap;
};

char *recv_str_until(struct RecvBuffer *recv_buffer, char c)
{
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

bool str_starts_with(char *s, char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

bool str_ends_with(char *s, char *suffix)
{
    int s_len = strlen(s);
    int suffix_len = strlen(suffix);

    if (s_len < suffix_len) {
        return false;
    }

    return strncmp(s + s_len - suffix_len, suffix, suffix_len) == 0;
}

void log_debug_handle_client_header(struct HandleClientArgs *args)
{
    log_debug("[%d] [%s:%d] ",
            args->sk,
            inet_ntoa(args->client_addr.sin_addr),
            ntohs(args->client_addr.sin_port));
}

static void *handle_client(void *void_arg)
{
    struct HandleClientArgs *args = void_arg;

    int sk = args->sk;

    log_debug_handle_client_header(args);
    log_debug("thread started: fn=accepted_socket_handler\n");

    struct RecvBuffer recv_buffer = {
        .fd = sk,

        .buf = malloc(recv_buf_capacity),
        .pos = 0,
        .len = 0,
        .cap = recv_buf_capacity,
    };

    char *request_line = recv_line(&recv_buffer);
    remove_crlf(request_line);

    log_debug_handle_client_header(args);
    log_debug("first line: %s\n", request_line);

    enum HttpMethod method;
    char *request_target;
    enum HttpVersion version;

    struct MethodTableEntry {
        char *request_line_prefix;
        enum HttpMethod method;
    };

    struct MethodTableEntry method_table[] = {
        { "GET ", HTTP_METHOD_GET },
        { "HEAD ", HTTP_METHOD_HEAD },
        { "POST ", HTTP_METHOD_POST },
        { "PUT ", HTTP_METHOD_PUT },
        { "DELETE ", HTTP_METHOD_DELETE },
        { "TRACE ", HTTP_METHOD_TRACE },
        { "OPTIONS ", HTTP_METHOD_OPTIONS },
        { "CONNECT ", HTTP_METHOD_CONNECT },
        { "PATCH ", HTTP_METHOD_PATCH },
    };

    bool found = false;

    for (int i = 0; i < ARRAY_SIZE(method_table); ++i) {
        if (str_starts_with(request_line, method_table[i].request_line_prefix)) {
            method = method_table[i].method;
            found = true;
            break;
        }
    }

    if (!found) {
        log_debug_handle_client_header(args);
        log_debug("Unknown HTTP method\n");
        goto finally;
    }

    if (str_ends_with(request_line, " HTTP/0.9")) {
        version = HTTP_VERSION_0_9;
    } else if (str_ends_with(request_line, " HTTP/1.0")) {
        version = HTTP_VERSION_1_0;
    } else if (str_ends_with(request_line, " HTTP/1.1")) {
        version = HTTP_VERSION_1_1;
    } else {
        log_debug_handle_client_header(args);
        log_debug("Unknown HTTP version\n");
        goto finally;
    }

    log_debug_handle_client_header(args);
    log_debug("Given HTTP method: %d\n", method);

    log_debug_handle_client_header(args);
    log_debug("Given HTTP version: %d\n", version);

    char *first_space_ptr = strchr(request_line, ' ');
    char *last_space_ptr = strrchr(request_line, ' ');

    request_target = strndup(first_space_ptr + 1, last_space_ptr - first_space_ptr - 1);

    log_debug_handle_client_header(args);
    log_debug("Given HTTP request target: %s\n", request_target);

    int count = 0;
    while (1) {
        char *header_line = recv_line(&recv_buffer);
        remove_crlf(header_line);
        if (strlen(header_line) == 0) {
            break;
        }

        log_debug_handle_client_header(args);
        log_debug("%d header: %s\n", count++, header_line);
    }

    char *http_response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nHello, world!";

    int send_result = send(sk, http_response, strlen(http_response), 0);
    if (send_result == -1) {
        perror("send");
    }

finally:
    free(args);

    close(sk);

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
        args->client_addr = client_addr;

        pthread_t client_thread;

        int pthread_result = pthread_create(&client_thread, NULL, handle_client, args);

        if (pthread_result != 0) {
            perror("pthread_create");
            exit(5);
        }
    }

    return 0;
}
