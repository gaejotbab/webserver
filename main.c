#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <pthread.h>
#include <string.h>
#include <stdarg.h>

#include "string.h"
#include "socket.h"
#include "http.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

void log_debug(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vprintf(format, ap);
    va_end(ap);
}

static const int backlog = 32;

static const int recv_buf_capacity = 2048;

static bool server_stopped = false;

struct HandleClientArgs {
    int sk;

    struct sockaddr_in client_addr;
};

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

    struct HttpRequest request;

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

    request.method = method;

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

    request.version = version;

    log_debug_handle_client_header(args);
    log_debug("Given HTTP method: %d\n", method);

    log_debug_handle_client_header(args);
    log_debug("Given HTTP version: %d\n", version);

    char *first_space_ptr = strchr(request_line, ' ');
    char *last_space_ptr = strrchr(request_line, ' ');

    request_target = strndup(first_space_ptr + 1, last_space_ptr - first_space_ptr - 1);

    request.target = request_target;

    log_debug_handle_client_header(args);
    log_debug("Given HTTP request target: %s\n", request_target);

    struct HttpRequestHeaderFields fields;
    fields.len = 0;
    fields.cap = initial_header_fields_capacity;
    fields.elements = malloc(fields.cap * sizeof(struct HttpRequestHeaderField));

    int pos = 0;
    while (1) {
        char *header_line = recv_line(&recv_buffer);
        remove_crlf(header_line);
        if (strlen(header_line) == 0) {
            break;
        }

        char *header_field_name;
        char *header_field_value;

        char *colon_ptr = strchr(header_line, ':');
        header_field_name = strndup(header_line, colon_ptr - header_line);

        char *ptr;
        for (ptr = colon_ptr + 1; *ptr == ' ' || *ptr == '\t'; ++ptr) {
            // Nothing.
        }
        header_field_value = strdup(ptr);

        if (pos == fields.cap) {
            fields.cap *= 2;
            fields.elements = realloc(fields.elements,
                    fields.cap * sizeof(struct HttpRequestHeaderField));
        }

        fields.elements[pos].name = header_field_name;
        fields.elements[pos].value = header_field_value;

        log_debug_handle_client_header(args);
        log_debug("%d Header field name: %s\n", pos, header_field_name);
        log_debug_handle_client_header(args);
        log_debug("%d Header field value: %s\n", pos, header_field_value);

        ++pos;
        ++fields.len;
    }

    request.fields = fields;

    if (strcmp(request.target, "/") == 0) {
        FILE *fp = fopen("contents/index.html", "r");
        if (fp == NULL) {
            perror("fopen");
            goto finally;
        }

        if (fseek(fp, 0, SEEK_END) == -1) {
            perror("fseek");
            fclose(fp);
            goto finally;
        }

        long file_size = ftell(fp);
        if (file_size == -1) {
            perror("ftell");
            fclose(fp);
            goto finally;
        }

        if (fseek(fp, 0, SEEK_SET) == -1) {
            perror("fseek");
            fclose(fp);
            goto finally;
        }

        char *file_content = malloc(file_size);
        size_t n = fread(file_content, 1, file_size, fp);
        if (n != file_size) {
            log_debug_handle_client_header(args);
            log_debug("Error while reading the file: n != file_size\n");
            fclose(fp);
            goto finally;
        }

        fclose(fp);

        char *http_response_first = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n";

        struct SendAllResult send_all_result;

        send_all_result = send_all(sk, http_response_first, strlen(http_response_first), 0);
        if (!send_all_result.success) {
            perror("send");
            goto finally;
        }

        send_all_result = send_all(sk, file_content, file_size, 0);
        if (!send_all_result.success) {
            perror("send");
            goto finally;
        }
    } else if (strcmp(request.target, "/ssammu.jpeg") == 0) {
        FILE *fp = fopen("contents/ssammu.jpeg", "r");
        if (fp == NULL) {
            perror("fopen");
            goto finally;
        }

        if (fseek(fp, 0, SEEK_END) == -1) {
            perror("fseek");
            fclose(fp);
            goto finally;
        }

        long file_size = ftell(fp);
        if (file_size == -1) {
            perror("ftell");
            fclose(fp);
            goto finally;
        }

        if (fseek(fp, 0, SEEK_SET) == -1) {
            perror("fseek");
            fclose(fp);
            goto finally;
        }

        char *file_content = malloc(file_size);
        size_t n = fread(file_content, 1, file_size, fp);
        if (n != file_size) {
            log_debug_handle_client_header(args);
            log_debug("Error while reading the file: n != file_size\n");
            fclose(fp);
            goto finally;
        }

        fclose(fp);

        char *http_response_first = "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\n\r\n";

        struct SendAllResult send_all_result;

        send_all_result = send_all(sk, http_response_first, strlen(http_response_first), 0);
        if (!send_all_result.success) {
            perror("send");
            goto finally;
        }

        send_all_result = send_all(sk, file_content, file_size, 0);
        if (!send_all_result.success) {
            perror("send");
            goto finally;
        }
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
