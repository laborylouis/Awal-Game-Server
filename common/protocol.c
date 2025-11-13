#include "protocol.h"
#include "net.h"
#include <string.h>
#include <stdio.h>

void protocol_create_message(message_t *msg, msg_type_t type, const char *sender, const char *recipient, const char *data)
{
    msg->type = type;
    strncpy(msg->sender, sender ? sender : "", sizeof(msg->sender) - 1);
    strncpy(msg->recipient, recipient ? recipient : "", sizeof(msg->recipient) - 1);
    strncpy(msg->data, data ? data : "", sizeof(msg->data) - 1);
    msg->sender[sizeof(msg->sender) - 1] = '\0';
    msg->recipient[sizeof(msg->recipient) - 1] = '\0';
    msg->data[sizeof(msg->data) - 1] = '\0';
}

int protocol_send_message(int sock, const message_t *msg)
{
    /* Simple implementation: send the whole structure */
    /* TODO: Could be improved with serialization (JSON, binary format, etc.) */
    int sent = net_send(sock, (const char*)msg, sizeof(message_t));
    return sent == sizeof(message_t) ? 0 : -1;
}

int protocol_recv_message(int sock, message_t *msg)
{
    /* Receive the whole structure, handling partial reads (TCP fragmentation) */
    size_t total_received = 0;
    size_t msg_size = sizeof(message_t);
    char *buffer = (char*)msg;
    
    while (total_received < msg_size) {
        int received = recv(sock, buffer + total_received, msg_size - total_received, 0);
        
        if (received < 0) {
            return -1;  /* Network error */
        }
        if (received == 0) {
            return 0;  /* Connection closed by peer */
        }
        total_received += received;
    }
    
    return total_received;  /* Success - should equal msg_size */
}

/* Create a login message. The password (if any) is placed in the data field. */
void protocol_create_login(message_t *msg, const char *username, const char *password)
{
    /* Put the password (may be empty) into the data field. The server will hash it. */
    protocol_create_message(msg, MSG_LOGIN, username, "", password ? password : "");
}

void protocol_create_challenge(message_t *msg, const char *from, const char *to)
{
    protocol_create_message(msg, MSG_CHALLENGE, from, to, "");
}

void protocol_create_move(message_t *msg, const char *player, int hole, const char *session_id)
{
    char data[16];
    snprintf(data, sizeof(data), "%d", hole);
    /* puts session id into recipient so server knows which session this move targets */
    protocol_create_message(msg, MSG_PLAY_MOVE, player, session_id, data);
}

void protocol_create_chat(message_t *msg, const char *from, const char *to, const char *text)
{
    protocol_create_message(msg, MSG_PRIVATE_CHAT, from, to, text);
}

void protocol_create_private_chat(message_t *msg, const char *from, const char *to, const char *text)
{
    protocol_create_message(msg, MSG_PRIVATE_CHAT, from, to, text);
}

void protocol_create_session_chat(message_t *msg, const char *from, const char *session_id, const char *text)
{
    protocol_create_message(msg, MSG_SESSION_CHAT, from, session_id, text);
}