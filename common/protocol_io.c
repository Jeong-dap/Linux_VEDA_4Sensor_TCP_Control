#include <errno.h>
#include <stddef.h>
#include <sys/socket.h>

#include "protocol_io.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

int protocol_recv_message(int socket_fd, Message *message) {
    unsigned char *cursor = (unsigned char *)message;
    size_t remaining = sizeof(*message);

    while (remaining > 0) {
        ssize_t received = recv(socket_fd, cursor, remaining, 0);
        if (received > 0) {
            cursor += (size_t)received;
            remaining -= (size_t)received;
            continue;
        }
        if (received == 0) {
            if (remaining == sizeof(*message)) {
                return 0;
            }
            errno = ECONNRESET;
            return -1;
        }
        if (errno != EINTR) {
            return -1;
        }
    }

    return 1;
}

int protocol_send_message(int socket_fd, const Message *message) {
    const unsigned char *cursor = (const unsigned char *)message;
    size_t remaining = sizeof(*message);

    while (remaining > 0) {
        ssize_t sent = send(socket_fd, cursor, remaining, MSG_NOSIGNAL);
        if (sent > 0) {
            cursor += (size_t)sent;
            remaining -= (size_t)sent;
            continue;
        }
        if (sent == 0) {
            errno = EPIPE;
            return -1;
        }
        if (errno != EINTR) {
            return -1;
        }
    }

    return 0;
}
