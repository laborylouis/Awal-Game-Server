#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/net.h"
#include "../common/protocol.h"
#include "../game/awale.h"

/* Game session structure */
typedef struct {
    int active;
    char player1_name[64];
    char player2_name[64];
    SOCKET player1_sock;
    SOCKET player2_sock;
    awale_game_t *game;
} game_session_t;

#define MAX_SESSIONS 50
static game_session_t sessions[MAX_SESSIONS];

/* Function prototypes */
void sessions_init(void);
int session_create(const char *player1, SOCKET sock1, const char *player2, SOCKET sock2);
int session_find_by_player(const char *player_name);
void session_destroy(int session_id);
int session_handle_move(int session_id, const char *player_name, int hole);
void session_broadcast_state(int session_id);
void session_notify_game_over(int session_id);

/* ==================== Session Management ==================== */

void sessions_init(void)
{
    memset(sessions, 0, sizeof(sessions));
    for (int i = 0; i < MAX_SESSIONS; i++) {
        sessions[i].active = 0;
        sessions[i].game = NULL;
    }
}

int session_create(const char *player1, SOCKET sock1, const char *player2, SOCKET sock2)
{
    /* Find free session slot */
    int slot = -1;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (!sessions[i].active) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        return -1; /* No free slots */
    }
    
    /* Initialize session */
    sessions[slot].active = 1;
    strncpy(sessions[slot].player1_name, player1, sizeof(sessions[slot].player1_name) - 1);
    strncpy(sessions[slot].player2_name, player2, sizeof(sessions[slot].player2_name) - 1);
    sessions[slot].player1_sock = sock1;
    sessions[slot].player2_sock = sock2;
    sessions[slot].game = awale_create();
    
    /* Randomly decide who starts */
    sessions[slot].game->current_player = rand()%2;
    
    
    printf("Game session %d created: %s vs %s\n", slot, player1, player2);
    
    /* Notify both players */
    message_t msg;
    protocol_create_message(&msg, MSG_GAME_START, "server", player1, player2);
    protocol_send_message(sock1, &msg);
    protocol_send_message(sock2, &msg);
    
    /* Send initial game state */
    session_broadcast_state(slot);
    
    return slot;
}

int session_find_by_player(const char *player_name)
{
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].active) {
            if (strcmp(sessions[i].player1_name, player_name) == 0 ||
                strcmp(sessions[i].player2_name, player_name) == 0) {
                return i;
            }
        }
    }
    return -1;
}

void session_destroy(int session_id)
{
    if (session_id < 0 || session_id >= MAX_SESSIONS) {
        return;
    }
    
    if (sessions[session_id].game) {
        awale_free(sessions[session_id].game);
        sessions[session_id].game = NULL;
    }
    
    sessions[session_id].active = 0;
    printf("Game session %d destroyed\n", session_id);
}

int session_handle_move(int session_id, const char *player_name, int hole)
{
    if (session_id < 0 || session_id >= MAX_SESSIONS || !sessions[session_id].active) {
        return -1;
    }
    
    game_session_t *session = &sessions[session_id];
    
    /* Determine which player */
    int player_num;
    if (strcmp(player_name, session->player1_name) == 0) {
        player_num = 0;
    } else if (strcmp(player_name, session->player2_name) == 0) {
        player_num = 1;
    } else {
        return -1; /* Player not in this game */
    }
    
    /* Check if it's this player's turn */
    if (player_num != session->game->current_player) {
        /* Not this player's turn */
        message_t msg;
        protocol_create_message(&msg, MSG_ERROR, "server", player_name, "Not your turn");
        SOCKET sock = (player_num == 0) ? session->player1_sock : session->player2_sock;
        protocol_send_message(sock, &msg);
        return -1;
    }
    
    /* Attempt to play the move */
    awale_status_t status = awale_play_move(session->game, hole);
    
    if (status != AWALE_OK) {
        /* Invalid move */
        message_t msg;
        protocol_create_message(&msg, MSG_ERROR, "server", player_name, 
                              awale_status_string(status));
        SOCKET sock = (player_num == 0) ? session->player1_sock : session->player2_sock;
        protocol_send_message(sock, &msg);
        return -1;
    }
    
    /* Move was successful, broadcast new state */
    session_broadcast_state(session_id);
    
    /* Check if game is over */
    if (awale_is_game_over(session->game)) {
        session_notify_game_over(session_id);
        session_destroy(session_id);
    }
    
    return 0;
}

void session_broadcast_state(int session_id)
{
    if (session_id < 0 || session_id >= MAX_SESSIONS || !sessions[session_id].active) {
        return;
    }
    
    game_session_t *session = &sessions[session_id];
    
    /* Convert game state to string (use player names) */
    char state_buffer[BUF_SIZE];
    /* session->player1_name corresponds to player 0, player2_name to player 1 */
    awale_print_to_buffer(session->game, state_buffer, sizeof(state_buffer),
                                     session->player1_name, session->player2_name);
    
    /* Send to both players */
    message_t msg;
    protocol_create_message(&msg, MSG_GAME_STATE, "server", "", state_buffer);
    protocol_send_message(session->player1_sock, &msg);
    protocol_send_message(session->player2_sock, &msg);
}

void session_notify_game_over(int session_id)
{
    if (session_id < 0 || session_id >= MAX_SESSIONS || !sessions[session_id].active) {
        return;
    }
    
    game_session_t *session = &sessions[session_id];
    int winner = awale_get_winner(session->game);
    
    char result[256];
    if (winner == -1) {
        snprintf(result, sizeof(result), "Game Over - Draw! Scores: %d - %d",
                awale_get_score(session->game, 0),
                awale_get_score(session->game, 1));
    } else {
        const char *winner_name = (winner == 0) ? session->player1_name : session->player2_name;
        snprintf(result, sizeof(result), "Game Over - Winner: %s! Scores: %d - %d",
                winner_name,
                awale_get_score(session->game, 0),
                awale_get_score(session->game, 1));
    }
    
    message_t msg;
    protocol_create_message(&msg, MSG_GAME_OVER, "server", "", result);
    protocol_send_message(session->player1_sock, &msg);
    protocol_send_message(session->player2_sock, &msg);
    
    printf("%s\n", result);
}

/* Return the opponent's name for a given player in a session, or NULL if not found */
const char *session_get_opponent_name(int session_id, const char *player_name)
{
    if (session_id < 0 || session_id >= MAX_SESSIONS) {
        return NULL;
    }

    game_session_t *session = &sessions[session_id];
    if (!session->active) {
        return NULL;
    }

    if (strcmp(session->player1_name, player_name) == 0) {
        return session->player2_name;
    }
    if (strcmp(session->player2_name, player_name) == 0) {
        return session->player1_name;
    }

    return NULL;
}
