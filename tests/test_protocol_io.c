#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "protocol_io.h"

typedef struct {
    int fd;
    Message message;
} SenderArgs;

static void *fragmented_sender(void *arg) {
    SenderArgs *sender = arg;
    const uint8_t *bytes = (const uint8_t *)&sender->message;

    assert(send(sender->fd, bytes, 1, 0) == 1);
    assert(send(sender->fd, bytes + 1, 2, 0) == 2);
    assert(send(sender->fd, bytes + 3, 1, 0) == 1);
    return NULL;
}

static void test_fragmented_receive(void) {
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);

    SenderArgs sender = {
        .fd = sockets[0],
        .message = {MSG_CMD, DEV_LED, ACT_SET_BRIGHTNESS, BRIGHTNESS_HIGH},
    };
    pthread_t sender_thread;
    assert(pthread_create(&sender_thread, NULL, fragmented_sender, &sender) == 0);

    Message received = {0};
    assert(protocol_recv_message(sockets[1], &received) == 1);
    assert(memcmp(&received, &sender.message, sizeof(received)) == 0);

    assert(pthread_join(sender_thread, NULL) == 0);
    close(sockets[0]);
    close(sockets[1]);
}

static void test_round_trip(void) {
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);

    Message sent = {MSG_QUERY, DEV_SENSOR, ACT_GET_LUX, 0};
    Message received = {0};
    assert(protocol_send_message(sockets[0], &sent) == 0);
    assert(protocol_recv_message(sockets[1], &received) == 1);
    assert(memcmp(&received, &sent, sizeof(received)) == 0);

    close(sockets[0]);
    close(sockets[1]);
}

static void test_clean_eof(void) {
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    close(sockets[0]);

    Message received = {0};
    assert(protocol_recv_message(sockets[1], &received) == 0);
    close(sockets[1]);
}

static void test_truncated_message(void) {
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);

    const uint8_t partial[] = {MSG_CMD, DEV_LED};
    assert(send(sockets[0], partial, sizeof(partial), 0) == (ssize_t)sizeof(partial));
    close(sockets[0]);

    Message received = {0};
    errno = 0;
    assert(protocol_recv_message(sockets[1], &received) == -1);
    assert(errno == ECONNRESET);
    close(sockets[1]);
}

int main(void) {
    test_fragmented_receive();
    test_round_trip();
    test_clean_eof();
    test_truncated_message();
    puts("protocol_io tests passed");
    return 0;
}
