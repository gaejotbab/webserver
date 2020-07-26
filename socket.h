#ifndef WEBSERVER_SOCKET_H
#define WEBSERVER_SOCKET_H

struct RecvBuffer {
    int fd;

    char *buf;
    int pos;
    int len;
    int cap;
};

extern char *recv_str_until(struct RecvBuffer *recv_buffer, char c);
extern char *recv_line(struct RecvBuffer *recv_buffer);

#endif //WEBSERVER_SOCKET_H
