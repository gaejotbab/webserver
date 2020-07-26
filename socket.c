#include "socket.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

static const int initial_str_buf_capacity = 64;

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

struct SendAllResult send_all(int sockfd, const void *buf, size_t len, int flags)
{
    size_t pos = 0;
    char *buffer = buf;

    while (pos < len) {
        size_t remaining = len - pos;

        ssize_t n = send(sockfd, buffer + pos, remaining, flags);

        if (n == -1) {
            return (struct SendAllResult) {
              .n = pos,
              .success = false,
            };
        }

        pos += n;
    }

    return (struct SendAllResult) {
        .n = pos,
        .success = true,
    };
}