#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "net.h"

/* Message types for client-server communication */
typedef enum {
    MSG_LOGIN,              /* Client logs in with username */
    MSG_LOGOUT,             /* Client disconnects */
    MSG_LIST_PLAYERS,       /* Request list of online players */
    MSG_PLAYER_LIST,        /* Server response with player list */
    MSG_LIST_GAMES,         /* Request list of ongoing games (sessions) */
    MSG_GAME_LIST,          /* Server response with games list */
    MSG_CHALLENGE,          /* Player A challenges player B */
    MSG_CHALLENGE_ACCEPT,   /* Player B accepts challenge */
    MSG_CHALLENGE_REFUSE,   /* Player B refuses challenge */
    MSG_GAME_START,         /* Server notifies game start */
    MSG_GAME_STATE,         /* Server sends current game state */
    MSG_PLAY_MOVE,          /* Client sends move */
    MSG_MOVE_RESULT,        /* Server confirms move or error */
    MSG_GAME_OVER,          /* Game ended */
    MSG_CHAT,               /* Chat message */
    MSG_ERROR,              /* Error message */
    MSG_BIO,                /* Player bio request/response */
    MSG_SPECTATE            /* Request to spectate a game */
} msg_type_t;

/* Protocol message structure */
typedef struct {
    msg_type_t type;
    char sender[64];
    char recipient[64];
    char data[BUF_SIZE];
} message_t;

/* Protocol functions */
int protocol_send_message(int sock, const message_t *msg);
int protocol_recv_message(int sock, message_t *msg);
void protocol_create_message(message_t *msg, msg_type_t type, 
                             const char *sender, const char *recipient, 
                             const char *data);

/* Helper functions for specific messages */
void protocol_create_login(message_t *msg, const char *username);
void protocol_create_challenge(message_t *msg, const char *from, const char *to);
void protocol_create_move(message_t *msg, const char *player, int hole);
void protocol_create_chat(message_t *msg, const char *from, const char *to, const char *text);

#endif /* PROTOCOL_H */
