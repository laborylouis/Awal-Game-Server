#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../common/net.h"
#include "../common/protocol.h"
#include "../game/awale.h"
#include "session.h"

static game_session_t sessions[MAX_SESSIONS];

/* Save session to a simple text .awale file in ./saved_games */
static int session_save_game(int session_id)
{
    if (session_id < 0 || session_id >= MAX_SESSIONS) return -1;
    game_session_t *s = &sessions[session_id];
    if (!s->active || !s->game) return -1;

    /* sanitize simple filename */
    char p1[128] = {0}, p2[128] = {0};
    for (size_t i = 0; i < sizeof(p1)-1 && s->player1_name[i]; i++) {
        char c = s->player1_name[i]; p1[i] = (c=='/'||c=='\\'||c==':'||c==' ') ? '_' : c;
    }
    for (size_t i = 0; i < sizeof(p2)-1 && s->player2_name[i]; i++) {
        char c = s->player2_name[i]; p2[i] = (c=='/'||c=='\\'||c==':'||c==' ') ? '_' : c;
    }

    char fname[1024];
    /* Build filename without timestamp; if file exists add a numeric suffix */
    snprintf(fname, sizeof(fname), "saved_games/%s_vs_%s.awale", p1, p2);
    /* If the file exists, try suffixes _1 ... _999 */
    for (int suffix = 1; suffix < 1000; suffix++) {
        FILE *fcheck = fopen(fname, "r");
        if (!fcheck) break; /* doesn't exist, good */
        fclose(fcheck);
        snprintf(fname, sizeof(fname), "saved_games/%s_vs_%s_%d.awale", p1, p2, suffix);
    }

    FILE *f = fopen(fname, "w");
    if (!f) return -1;

    fprintf(f, "# Awale saved game v1\n");
    fprintf(f, "players: %s|%s\n", s->player1_name, s->player2_name);
    /* No timestamps are recorded per request */
    fprintf(f, "winner: %d\n", s->game->winner);
    fprintf(f, "scores: %d %d\n", s->game->scores[0], s->game->scores[1]);
    fprintf(f, "holes:");
    for (int i = 0; i < TOTAL_HOLES; i++) fprintf(f, " %d", s->game->holes[i]);
    fprintf(f, "\n");

    fprintf(f, "moves_count: %d\n", s->move_count);
    fprintf(f, "moves:\n");
    for (int i = 0; i < s->move_count; i++) {
        /* Write moves as: player|hole (hole=-1 means give up) */
        fprintf(f, "%s|%d\n", s->moves[i].player, s->moves[i].hole);
    }

    fclose(f);
    printf("Saved game to %s\n", fname);
    return 0;
}

/* ==================== Session Management ==================== */

void sessions_init(void)
{
    memset(sessions, 0, sizeof(sessions));
    for (int i = 0; i < MAX_SESSIONS; i++) {
        sessions[i].active = 0;
        sessions[i].game = NULL;
        sessions[i].num_observers = 0;
        sessions[i].move_count = 0;
        sessions[i].start_time = 0;
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
    sessions[slot].num_observers = 0;
    sessions[slot].move_count = 0;
    sessions[slot].start_time = time(NULL);

    /* Randomly decide who starts */
    sessions[slot].game->current_player = rand()%2;
    
    
    printf("Game session %d created: %s vs %s\n", slot, player1, player2);
    
    /* Notify both players */
    message_t msg;
    /* Send GAME_START with recipient = session id (as string) and data = opponent name */
    char sid_str[32];
    snprintf(sid_str, sizeof(sid_str), "%d", slot);
    protocol_create_message(&msg, MSG_GAME_START, "server", sid_str, player2);
    protocol_send_message(sock1, &msg);
    protocol_create_message(&msg, MSG_GAME_START, "server", sid_str, player1);
    protocol_send_message(sock2, &msg);
    
    /* Send initial game state */
    session_broadcast_state(slot);
    
    return slot;
}

int session_find_by_player(int games[], const char *player_name)
{
    int count = 0;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].active) {
            if (strcmp(sessions[i].player1_name, player_name) == 0 ||
                strcmp(sessions[i].player2_name, player_name) == 0) {
                games[count] = i;
                count++;
            }
        }
    }
    return count;
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
    /* Notify and clear observers */
    for (int i = 0; i < sessions[session_id].num_observers; i++) {
        message_t msg;
        protocol_create_message(&msg, MSG_GAME_OVER, "server", sessions[session_id].observers[i].name, "Observed game ended");
        protocol_send_message(sessions[session_id].observers[i].sock, &msg);
        /* do not close observer sockets here; clients manage their own connection */
    }
    sessions[session_id].num_observers = 0;
    
    sessions[session_id].active = 0;
    printf("Game session %d destroyed\n", session_id);
}

int session_handle_move(int session_id, const char *player_name, int hole)
/*** 
 * Handles a player's move in the specified session.
 * Returns 0 on success, 1 on game over, -1 on error (invalid move, not player's turn, etc).
 */
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
    
    /* Record the move */
    if (session->move_count < (int)(sizeof(session->moves)/sizeof(session->moves[0]))) {
        strncpy(session->moves[session->move_count].player, player_name, sizeof(session->moves[session->move_count].player)-1);
        session->moves[session->move_count].player[sizeof(session->moves[session->move_count].player)-1] = '\0';
        session->moves[session->move_count].hole = hole;
        session->moves[session->move_count].when = time(NULL);
        session->move_count++;
    }

    /* Check if game is over */
    if (awale_is_game_over(session->game)) {
        /* Save completed game before notifying/destroying */
        session_save_game(session_id);
        session_notify_game_over(session_id);
        session_destroy(session_id);
        return 1; /* Indicate game over */
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
    
    /* Send to both players; include session id in recipient so clients know which session */
    message_t msg;
    char sid_str[32];
    snprintf(sid_str, sizeof(sid_str), "%d", session_id);
    protocol_create_message(&msg, MSG_GAME_STATE, "server", sid_str, state_buffer);
    protocol_send_message(session->player1_sock, &msg);
    protocol_send_message(session->player2_sock, &msg);

    /* Send to observers as well */
    for (int i = 0; i < session->num_observers; i++) {
        protocol_send_message(session->observers[i].sock, &msg);
    }
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
    char sid_str[32];
    snprintf(sid_str, sizeof(sid_str), "%d", session_id);
    protocol_create_message(&msg, MSG_GAME_OVER, "server", sid_str, result);
    protocol_send_message(session->player1_sock, &msg);
    protocol_send_message(session->player2_sock, &msg);
    for (int i = 0; i < session->num_observers; i++) {
        protocol_send_message(session->observers[i].sock, &msg);
    }
    
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

int session_get_players(int session_id, char *p1, int p1_size, char *p2, int p2_size)
{
    if (session_id < 0 || session_id >= MAX_SESSIONS) return -1;
    game_session_t *session = &sessions[session_id];
    if (!session->active) return -1;
    if (p1 && p1_size > 0) {
        strncpy(p1, session->player1_name, p1_size - 1);
        p1[p1_size - 1] = '\0';
    }
    if (p2 && p2_size > 0) {
        strncpy(p2, session->player2_name, p2_size - 1);
        p2[p2_size - 1] = '\0';
    }
    return 0;
}

/* Add an observer to a session. Observer keeps its own connection; server just stores sock/name. */
int session_add_observer(int session_id, const char *observer_name, SOCKET sock)
{
    if (session_id < 0 || session_id >= MAX_SESSIONS) return -1;
    game_session_t *s = &sessions[session_id];
    if (!s->active) return -1;
    if (s->num_observers >= (int)(sizeof(s->observers)/sizeof(s->observers[0]))) return -1;

    strncpy(s->observers[s->num_observers].name, observer_name ? observer_name : "", sizeof(s->observers[s->num_observers].name)-1);
    s->observers[s->num_observers].sock = sock;
    s->num_observers++;

    /* Immediately send current state to new observer */
    char state_buffer[BUF_SIZE];
    awale_print_to_buffer(s->game, state_buffer, sizeof(state_buffer), s->player1_name, s->player2_name);
    message_t msg;
    char sid_str[32];
    snprintf(sid_str, sizeof(sid_str), "%d", session_id);
    protocol_create_message(&msg, MSG_GAME_STATE, "server", sid_str, state_buffer);
    protocol_send_message(sock, &msg);
    return 0;
}

int session_remove_observer(int session_id, SOCKET sock)
{
    if (session_id < 0 || session_id >= MAX_SESSIONS) return -1;
    game_session_t *s = &sessions[session_id];
    if (!s->active) return -1;

    int idx = -1;
    for (int i = 0; i < s->num_observers; i++) {
        if (s->observers[i].sock == sock) { idx = i; break; }
    }
    if (idx == -1) return -1;
    for (int i = idx; i < s->num_observers - 1; i++) {
        s->observers[i] = s->observers[i+1];
    }
    s->num_observers--;
    return 0;
}

/* Build a textual list of active sessions into provided buffer */
void session_list_games(char *buffer, int size)
{
    int offset = 0;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].active) {
            offset += snprintf(buffer + offset, size - offset, "%d: %s vs %s\n", i, sessions[i].player1_name, sessions[i].player2_name);
            if (offset >= size) break;
        }
    }
    if (offset == 0) snprintf(buffer, size, "No active games\n");
}

/* Handle a player giving up: mark opponent as winner, collect remaining seeds, notify and destroy session */
int session_give_up(int session_id, const char *player_name)
{
    if (session_id < 0 || session_id >= MAX_SESSIONS || !sessions[session_id].active) return -1;
    game_session_t *session = &sessions[session_id];

    int player_num;
    if (strcmp(player_name, session->player1_name) == 0) player_num = 0;
    else if (strcmp(player_name, session->player2_name) == 0) player_num = 1;
    else return -1;

    int opponent = 1 - player_num;

    /* Collect remaining seeds to opponent and clear holes */
    int opp_start = opponent * HOLES_PER_PLAYER;
    int opp_end = opp_start + HOLES_PER_PLAYER;
    for (int i = 0; i < TOTAL_HOLES; i++) {
        if (i >= opp_start && i < opp_end) {
            session->game->scores[opponent] += session->game->holes[i];
        } else {
            /* leave other side's seeds as is or add to opponent as well */
            /* we'll also collect them to opponent to finalize the score */
            session->game->scores[opponent] += session->game->holes[i];
        }
        session->game->holes[i] = 0;
    }

    /* Record give-up event */
    if (session->move_count < (int)(sizeof(session->moves)/sizeof(session->moves[0]))) {
        strncpy(session->moves[session->move_count].player, player_name, sizeof(session->moves[session->move_count].player)-1);
        session->moves[session->move_count].player[sizeof(session->moves[session->move_count].player)-1] = '\0';
        session->moves[session->move_count].hole = -1; /* give-up marker */
        session->moves[session->move_count].when = time(NULL);
        session->move_count++;
    }

    session->game->game_over = 1;
    session->game->winner = opponent;

    /* Save completed game, then notify and cleanup */
    session_save_game(session_id);
    session_notify_game_over(session_id);
    session_destroy(session_id);

    return 0;
}
