#ifndef PROTOCOL_IO_H
#define PROTOCOL_IO_H

#include "protocol.h"

/*
 * TCP is a byte stream: one send() does not imply one matching recv().
 * These helpers transfer exactly one four-byte Message.
 *
 * protocol_recv_message():
 *   1  complete message received
 *   0  peer closed the connection before the next message
 *  -1  I/O error or truncated message
 *
 * protocol_send_message():
 *   0  complete message sent
 *  -1  I/O error
 */
int protocol_recv_message(int socket_fd, Message *message);
int protocol_send_message(int socket_fd, const Message *message);

#endif /* PROTOCOL_IO_H */
