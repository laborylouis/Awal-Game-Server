#ifndef SERVER_SESSION_H
#define SERVER_SESSION_H

#include "../common/net.h"

#define MAX_SESSIONS 50

void sessions_init(void);
int session_create(const char *player1, SOCKET sock1, const char *player2, SOCKET sock2);
int session_find_by_player(const char *player_name);
void session_destroy(int session_id);
int session_handle_move(int session_id, const char *player_name, int hole);
void session_broadcast_state(int session_id);
void session_notify_game_over(int session_id);
const char *session_get_opponent_name(int session_id, const char *player_name);
/* Observers and listing */
int session_add_observer(int session_id, const char *observer_name, SOCKET sock);
int session_remove_observer(int session_id, SOCKET sock);
void session_list_games(char *buffer, int size);

#endif
