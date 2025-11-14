#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/select.h>
#include <time.h>
#include <ctype.h>

#include "../common/net.h"
#include "../common/protocol.h"
#include "../game/awale.h"
#include "session.h"

#define MAX_PLAYERS 100 // Maximum connected players
#define MAX_PENDING_CHALLENGES 10 /* Max pending challengers stored per player */

typedef struct {
    SOCKET sock;
    char name[64];
    int in_game; /* 0 = not in game, 1+ = in game */
    int num_pending_challengers;
    char pending_challengers[MAX_PENDING_CHALLENGES][64];
    int num_pending_friend_requests;
    char pending_friend_requests[MAX_PENDING_CHALLENGES][64];
    char bio[BUF_SIZE];
    int private_mode; /* 1 = private (only friends can spectate), 0 = public */
} player_t;

static player_t players[MAX_PLAYERS];
static int num_players = 0;


#define ACCOUNTS_FILE "accounts.db"
#define MAX_ACCOUNTS 1000 
#define MAX_PASSWORD_HASH_LENGTH 65
typedef struct {
    char name[64];
    char hash[MAX_PASSWORD_HASH_LENGTH];
    char bio[BUF_SIZE];
    char friends[BUF_SIZE];
} account_t;

static account_t accounts[MAX_ACCOUNTS];
static int num_accounts = 0;

/* Function prototypes */
static void load_accounts(void);
static int find_account_index(const char *name);
static int add_account(const char *name, const char *hash, const char *bio);
static int save_accounts(void);
static void escape_string(const char *in, char *out, int out_size);
static void unescape_string(const char *in, char *out, int out_size);
static void init_server(void);
static void cleanup_server(void);
static void run_server(void);
static int add_player(SOCKET sock, const char *name);
static void remove_player(int index);
static player_t* find_player_by_name(const char *name);
static void handle_new_connection(SOCKET server_sock);
static void handle_client_message(int player_index);
void hash_password(const char *password, char *hashed_password);

/* Account store helpers */
// Load accounts from `accounts.db` into the in-memory accounts array.
static void load_accounts(void)
{
    num_accounts = 0;
    FILE *f = fopen(ACCOUNTS_FILE, "r");
    if (!f) return;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n'); if (nl) *nl = '\0';
        char *p1 = strchr(line, '|');
        if (!p1) continue;
        *p1 = '\0';
        char *p2 = strchr(p1 + 1, '|');
        if (!p2) continue;
        *p2 = '\0';
        char *p3 = strchr(p2 + 1, '|');
        if (!p3) continue;
        *p3 = '\0';
        char *name = line;
        char *hash = p1 + 1;
        char *bio_esc = p2 + 1;
        char *friends_esc = p3 + 1;
        if (num_accounts < MAX_ACCOUNTS) {
            strncpy(accounts[num_accounts].name, name, sizeof(accounts[num_accounts].name)-1);
            accounts[num_accounts].name[sizeof(accounts[num_accounts].name)-1] = '\0';
            strncpy(accounts[num_accounts].hash, hash, sizeof(accounts[num_accounts].hash)-1);
            accounts[num_accounts].hash[sizeof(accounts[num_accounts].hash)-1] = '\0';
            /* Unescape bio */
            unescape_string(bio_esc, accounts[num_accounts].bio, sizeof(accounts[num_accounts].bio));
            unescape_string(friends_esc, accounts[num_accounts].friends, sizeof(accounts[num_accounts].friends));
            num_accounts++;
        }
    }
    fclose(f);
}

// Find the index of an account by name, or -1 if not found.
static int find_account_index(const char *name)
{
    for (int i = 0; i < num_accounts; i++) {
        if (strcmp(accounts[i].name, name) == 0) return i;
    }
    return -1;
}

// Add a new account to memory and persist to disk.
static int add_account(const char *name, const char *hash, const char *bio)
{
    if (num_accounts >= MAX_ACCOUNTS) return -1;
    strncpy(accounts[num_accounts].name, name, sizeof(accounts[num_accounts].name)-1);
    accounts[num_accounts].name[sizeof(accounts[num_accounts].name)-1] = '\0';
    strncpy(accounts[num_accounts].hash, hash, sizeof(accounts[num_accounts].hash)-1);
    accounts[num_accounts].hash[sizeof(accounts[num_accounts].hash)-1] = '\0';
    if (bio) strncpy(accounts[num_accounts].bio, bio, sizeof(accounts[num_accounts].bio)-1);
    else accounts[num_accounts].bio[0] = '\0';
    accounts[num_accounts].bio[sizeof(accounts[num_accounts].bio)-1] = '\0';
    num_accounts++;
    if (save_accounts() != 0) return -1;
    return 0;
}

// Check whether account at acc_idx has friend at friend_idx.
static int account_has_friend_idx(int acc_idx, int friend_idx)
{
    if (acc_idx < 0 || acc_idx >= num_accounts || friend_idx < 0 || friend_idx >= num_accounts) return 0;
    char temp[BUF_SIZE];
    strncpy(temp, accounts[acc_idx].friends, sizeof(temp)-1);
    temp[sizeof(temp)-1] = '\0';
    char *tok = strtok(temp, ",");
    while (tok) {
        int val = atoi(tok);
        if (val == friend_idx) return 1;
        tok = strtok(NULL, ",");
    }
    return 0;
}

// Add friend_idx to acc_idx's friend list (by index) and persist.
static int account_add_friend_idx(int acc_idx, int friend_idx)
{
    if (acc_idx < 0 || acc_idx >= num_accounts || friend_idx < 0 || friend_idx >= num_accounts) return -1;
    if (account_has_friend_idx(acc_idx, friend_idx)) return 0; 
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", friend_idx);
    size_t cur = strlen(accounts[acc_idx].friends);
    if (cur > 0) {
        if (cur + 1 + strlen(buf) + 1 >= sizeof(accounts[acc_idx].friends)) return -1;
        strcat(accounts[acc_idx].friends, ",");
        strcat(accounts[acc_idx].friends, buf);
    } else {
        if (strlen(buf) + 1 >= sizeof(accounts[acc_idx].friends)) return -1;
        strcpy(accounts[acc_idx].friends, buf);
    }
    return save_accounts();
}

// Remove friend_idx from acc_idx's friend list and persist.
static int account_remove_friend_idx(int acc_idx, int friend_idx)
{
    if (acc_idx < 0 || acc_idx >= num_accounts || friend_idx < 0 || friend_idx >= num_accounts) return -1;
    char temp[BUF_SIZE];
    char out[BUF_SIZE];
    temp[0] = '\0'; out[0] = '\0';
    strncpy(temp, accounts[acc_idx].friends, sizeof(temp)-1);
    temp[sizeof(temp)-1] = '\0';
    char *tok = strtok(temp, ",");
    int first = 1;
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", friend_idx);
    while (tok) {
        if (strcmp(tok, buf) != 0) {
            if (!first) strcat(out, ",");
            strcat(out, tok);
            first = 0;
        }
        tok = strtok(NULL, ",");
    }
    strncpy(accounts[acc_idx].friends, out, sizeof(accounts[acc_idx].friends)-1);
    accounts[acc_idx].friends[sizeof(accounts[acc_idx].friends)-1] = '\0';
    return save_accounts();
}

// Write all in-memory accounts back to `accounts.db` with escaped fields.
static int save_accounts(void)
{
    FILE *f = fopen(ACCOUNTS_FILE, "w");
    if (!f) return -1;
    char bio_esc[BUF_SIZE * 2];
    char friends_esc[BUF_SIZE * 2];
    for (int i = 0; i < num_accounts; i++) {
        escape_string(accounts[i].bio, bio_esc, sizeof(bio_esc));
        escape_string(accounts[i].friends, friends_esc, sizeof(friends_esc));
        fprintf(f, "%s|%s|%s|%s\n", accounts[i].name, accounts[i].hash, bio_esc, friends_esc);
    }
    fclose(f);
    return 0;
}

// Escape pipes, backslashes and newlines for single-line storage.
static void escape_string(const char *in, char *out, int out_size)
{
    int o = 0;
    for (int i = 0; in[i] != '\0' && o + 1 < out_size; i++) {
        if (in[i] == '\n') {
            if (o + 2 >= out_size) break;
            out[o++] = '\\'; out[o++] = 'n';
        } else if (in[i] == '|') {
            if (o + 2 >= out_size) break;
            out[o++] = '\\'; out[o++] = '|';
        } else if (in[i] == '\\') {
            if (o + 2 >= out_size) break;
            out[o++] = '\\'; out[o++] = '\\';
        } else {
            out[o++] = in[i];
        }
    }
    out[o] = '\0';
}

// Unescape strings previously escaped by escape_string.
static void unescape_string(const char *in, char *out, int out_size)
{
    int o = 0;
    for (int i = 0; in[i] != '\0' && o + 1 < out_size; i++) {
        if (in[i] == '\\' && in[i+1] != '\0') {
            i++;
            if (in[i] == 'n') out[o++] = '\n';
            else if (in[i] == '|') out[o++] = '|';
            else if (in[i] == '\\') out[o++] = '\\';
            else out[o++] = in[i];
        } else {
            out[o++] = in[i];
        }
    }
    out[o] = '\0';
}

// Program entry point: initialize server, run main loop, cleanup on exit.
int main()
{
    printf("=== Awale Game Server ===\n");
    printf("Initializing...\n");
    
    init_server();
    run_server();
    cleanup_server();
    
    return EXIT_SUCCESS;
}

// Initialize networking, sessions and load persistent data.
static void init_server(void)
{
    net_init();
    sessions_init();
    load_accounts();
    srand((unsigned)time(NULL));
    memset(players, 0, sizeof(players));
    num_players = 0;
}

// Close client sockets and clean up networking resources.
static void cleanup_server(void)
{
    for (int i = 0; i < num_players; i++) {
        net_close(players[i].sock);
    }
    
    net_cleanup();
}

// Main server loop: accept connections and dispatch client messages.
static void run_server(void)
{
    SOCKET server_sock = net_create_socket();
    if (server_sock == INVALID_SOCKET) {
        fprintf(stderr, "Failed to create server socket\n");
        exit(EXIT_FAILURE);
    }
    
    if (net_bind_socket(server_sock, DEFAULT_PORT) < 0) {
        fprintf(stderr, "Failed to bind to port %d\n", DEFAULT_PORT);
        net_close(server_sock);
        exit(EXIT_FAILURE);
    }
    
    if (net_listen_socket(server_sock, MAX_PLAYERS) < 0) {
        fprintf(stderr, "Failed to listen on socket\n");
        net_close(server_sock);
        exit(EXIT_FAILURE);
    }
    
    printf("Server listening on port %d\n", DEFAULT_PORT);
    printf("Press Ctrl+C to stop\n\n");
    
    fd_set readfds;
    int max_fd = server_sock;
    
    while (1) {
        FD_ZERO(&readfds);
        FD_SET(server_sock, &readfds);
        
        for (int i = 0; i < num_players; i++) {
            FD_SET(players[i].sock, &readfds);
            if (players[i].sock > max_fd) {
                max_fd = players[i].sock;
            }
        }
        
        if (select(max_fd + 1, &readfds, NULL, NULL, NULL) < 0) {
            perror("select");
            break;
        }
        
        if (FD_ISSET(server_sock, &readfds)) {
            handle_new_connection(server_sock);
        }
        
        for (int i = 0; i < num_players; i++) {
            if (FD_ISSET(players[i].sock, &readfds)) {
                handle_client_message(i);
            }
        }
    }
    
    net_close(server_sock);
}

// Accept a new TCP connection and handle initial login/registration.
static void handle_new_connection(SOCKET server_sock)
{
    SOCKADDR_IN client_addr;
    SOCKET client_sock = net_accept_connection(server_sock, &client_addr);
    
    if (client_sock == INVALID_SOCKET) {
        return;
    }
    
    printf("New connection from %s\n", inet_ntoa(client_addr.sin_addr));
    
    message_t msg;
    if (protocol_recv_message(client_sock, &msg) < 0) {
        net_close(client_sock);
        return;
    }
    
    if (msg.type == MSG_LOGIN) {
        const char *username = msg.sender;
        const char *password = msg.data;

        int acc = find_account_index(username);
        if (acc >= 0) {
            if (strcmp(accounts[acc].hash, password) != 0) {
                message_t err;
                protocol_create_message(&err, MSG_ERROR, "server", username, "Invalid password");
                protocol_send_message(client_sock, &err);
                net_close(client_sock);
            } else {
                if (find_player_by_name(username) != NULL) {
                    message_t err;
                    protocol_create_message(&err, MSG_ERROR, "server", username, "User already online");
                    protocol_send_message(client_sock, &err);
                    net_close(client_sock);
                } else {
                    int idx = add_player(client_sock, username);
                    if (idx >= 0) {
                        printf("Player '%s' logged in\n", username);
                        message_t ok_msg;
                        char msg_content[128];
                        snprintf(msg_content, sizeof(msg_content), "Logged as %s", username);
                        protocol_create_message(&ok_msg, MSG_LOGIN_SUCCESS, "server", username, msg_content);
                        protocol_send_message(client_sock, &ok_msg);
                    } else {
                        message_t err;
                        protocol_create_message(&err, MSG_ERROR, "server", username, "Failed to add player");
                        protocol_send_message(client_sock, &err);
                        net_close(client_sock);
                    }
                }
            }
        } else {
            if (add_account(username, password, "") != 0) {
                message_t err;
                protocol_create_message(&err, MSG_ERROR, "server", username, "Failed to register account");
                protocol_send_message(client_sock, &err);
                net_close(client_sock);
            } else {
                int idx = add_player(client_sock, username);
                if (idx >= 0) {
                    printf("Registered and logged in new player '%s'\n", username);
                    message_t ok_msg;
                    protocol_create_private_chat(&ok_msg, "server", username, "Account created and logged in. Welcome!");
                    protocol_send_message(client_sock, &ok_msg);
                } else {
                    message_t err;
                    protocol_create_message(&err, MSG_ERROR, "server", username, "Failed to add player");
                    protocol_send_message(client_sock, &err);
                    net_close(client_sock);
                }
            }
        }
    } else {
        fprintf(stderr, "Expected login message\n");
        net_close(client_sock);
    }
}

// Process an incoming message from the client at players[player_index].
static void handle_client_message(int player_index)
{
    message_t msg;
    int result = protocol_recv_message(players[player_index].sock, &msg);
    
    if (result <= 0) {
        printf("Player '%s' disconnected\n", players[player_index].name);
       
        if (players[player_index].in_game) {
            int games[MAX_SESSIONS];
            int count = session_find_by_player(games, players[player_index].name);
            for (int i = 0; i < count; i++) {
                int sid = games[i];
                const char *opponent_name = (sid >= 0) ? session_get_opponent_name(sid, players[player_index].name) : NULL;
                player_t *opponent = opponent_name ? find_player_by_name(opponent_name) : NULL;

                if (sid >= 0) {
                    session_give_up(sid, players[player_index].name);
                }

                if (opponent) {
                      opponent->in_game--;
                }
            }
        }

        net_close(players[player_index].sock);
        for (int i = 0; i < num_players; i++) {
            if (i == player_index) continue;
            for (int p = 0; p < players[i].num_pending_challengers; p++) {
                if (strcmp(players[i].pending_challengers[p], players[player_index].name) == 0) {
                    for (int q = p; q < players[i].num_pending_challengers - 1; q++) {
                        strncpy(players[i].pending_challengers[q], players[i].pending_challengers[q+1], sizeof(players[i].pending_challengers[q]));
                    }
                    players[i].num_pending_challengers--;
                    p--;
                }
            }
            for (int p = 0; p < players[i].num_pending_friend_requests; p++) {
                if (strcmp(players[i].pending_friend_requests[p], players[player_index].name) == 0) {
                    for (int q = p; q < players[i].num_pending_friend_requests - 1; q++) {
                        strncpy(players[i].pending_friend_requests[q], players[i].pending_friend_requests[q+1], sizeof(players[i].pending_friend_requests[q]));
                    }
                    players[i].num_pending_friend_requests--;
                    p--;
                }
            }
        }

        remove_player(player_index);
        return;
    }
    
    switch (msg.type) {
        case MSG_LIST_PLAYERS:
        {
            char list[BUF_SIZE];
            int offset = 0;
            for (int i = 0; i < num_players; i++) {
                offset += snprintf(list + offset, sizeof(list) - offset, "%s%s", players[i].name, players[i].in_game ? " (in game)\n" : "\n");
                if (offset >= (int)sizeof(list)) break;
            }
            if (offset == 0) snprintf(list, sizeof(list), "No players online\n");
            message_t out;
            protocol_create_message(&out, MSG_PLAYER_LIST, "server", players[player_index].name, list);
            protocol_send_message(players[player_index].sock, &out);
        }
            break;

        case MSG_LIST_GAMES:
        {
            char list[BUF_SIZE];
            session_list_games(list, sizeof(list));
            message_t out;
            protocol_create_message(&out, MSG_GAME_LIST, "server", players[player_index].name, list);
            protocol_send_message(players[player_index].sock, &out);
        }
            break;

        case MSG_LIST_FRIENDS:
        {
            int acc = find_account_index(players[player_index].name);
            char list[BUF_SIZE];
            int offset = 0;
            if (acc < 0) {
                snprintf(list, sizeof(list), "No account found\n");
            } else if (accounts[acc].friends[0] == '\0') {
                snprintf(list, sizeof(list), "No friends\n");
            } else {
                char temp[BUF_SIZE];
                strncpy(temp, accounts[acc].friends, sizeof(temp)-1);
                temp[sizeof(temp)-1] = '\0';
                char *tok = strtok(temp, ",");
                while (tok) {
                    int friend_idx = atoi(tok);
                    const char *fname = NULL;
                    if (friend_idx >= 0 && friend_idx < num_accounts) {
                        fname = accounts[friend_idx].name;
                    }
                    if (fname) {
                        player_t *p = find_player_by_name(fname);
                        int ig = p->in_game;
                        offset += snprintf(list + offset, sizeof(list) - offset, "%s%s\n", fname, ig ? " (in game)" : (p ? " (online)" : ""));
                        if (offset >= (int)sizeof(list)) break;
                    }
                    tok = strtok(NULL, ",");
                }
            }
            message_t out;
            protocol_create_message(&out, MSG_FRIENDS_LIST, "server", players[player_index].name, list);
            protocol_send_message(players[player_index].sock, &out);
        }
            break;

        case MSG_ADD_FRIEND:
        {

            const char *toadd = msg.data;
            int acc = find_account_index(players[player_index].name);
            message_t out;
            if (acc < 0) {
                protocol_create_message(&out, MSG_FRIEND_RESULT, "server", players[player_index].name, "Your account was not found. Try later.");
                protocol_send_message(players[player_index].sock, &out);
                break;
            }
            int target_acc = find_account_index(toadd);
            if (target_acc < 0) {
                protocol_create_message(&out, MSG_FRIEND_RESULT, "server", players[player_index].name, "User not found");
                protocol_send_message(players[player_index].sock, &out);
                break;
            }
            if (target_acc == acc) {
                protocol_create_message(&out, MSG_FRIEND_RESULT, "server", players[player_index].name, "You cannot add yourself");
                protocol_send_message(players[player_index].sock, &out);
                break;
            }
            if (account_has_friend_idx(acc, target_acc)) {
                protocol_create_message(&out, MSG_FRIEND_RESULT, "server", players[player_index].name, "Already a friend");
                protocol_send_message(players[player_index].sock, &out);
                break;
            }

            player_t *target_player = find_player_by_name(accounts[target_acc].name);
            if (!target_player) {
                protocol_create_message(&out, MSG_FRIEND_RESULT, "server", players[player_index].name, "User is not online");
                protocol_send_message(players[player_index].sock, &out);
                break;
            }


            int fexists = 0;
            for (int p = 0; p < target_player->num_pending_friend_requests; p++) {
                if (strcmp(target_player->pending_friend_requests[p], players[player_index].name) == 0) { fexists = 1; break; }
            }
            if (!fexists && target_player->num_pending_friend_requests < MAX_PENDING_CHALLENGES) {
                strncpy(target_player->pending_friend_requests[target_player->num_pending_friend_requests], players[player_index].name, sizeof(target_player->pending_friend_requests[0]) - 1);
                target_player->pending_friend_requests[target_player->num_pending_friend_requests][sizeof(target_player->pending_friend_requests[0]) - 1] = '\0';
                target_player->num_pending_friend_requests++;
            }

            message_t req;
            protocol_create_message(&req, MSG_FRIEND_REQUEST, players[player_index].name, target_player->name, "");
            protocol_send_message(target_player->sock, &req);

            protocol_create_message(&out, MSG_FRIEND_RESULT, "server", players[player_index].name, "Friend request sent");
            protocol_send_message(players[player_index].sock, &out);
        }
            break;

        case MSG_FRIEND_REQUEST_ACCEPT:
        {
            /* msg.sender = acceptor, msg.recipient = original requester */
            int acc_acceptor = find_account_index(msg.sender);
            int acc_requester = find_account_index(msg.recipient);
            message_t out;
            /* Ensure there was a pending friend request from requester to acceptor */
            player_t *acceptor_player = &players[player_index];
            int found = 0;
            for (int p = 0; p < acceptor_player->num_pending_friend_requests; p++) {
                if (strcmp(acceptor_player->pending_friend_requests[p], msg.recipient) == 0) { found = 1; break; }
            }
            if (!found) {
                protocol_create_message(&out, MSG_FRIEND_RESULT, "server", msg.sender, "No pending friend request from this user");
                protocol_send_message(acceptor_player->sock, &out);
                break;
            }
            if (acc_acceptor < 0 || acc_requester < 0) {
                protocol_create_message(&out, MSG_FRIEND_RESULT, "server", msg.sender, "Account not found");
                protocol_send_message(players[player_index].sock, &out);
                break;
            }

            /* Add both sides; account_add_friend_idx persists */
            if (account_add_friend_idx(acc_acceptor, acc_requester) != 0) {
                protocol_create_message(&out, MSG_FRIEND_RESULT, "server", msg.sender, "Failed to add friend");
                protocol_send_message(players[player_index].sock, &out);
                break;
            }
            if (account_add_friend_idx(acc_requester, acc_acceptor) != 0) {
                /* best-effort: try to roll back the first add (optional) */
                protocol_create_message(&out, MSG_FRIEND_RESULT, "server", msg.sender, "Failed to add friend on other side");
                protocol_send_message(players[player_index].sock, &out);
                break;
            }

            /* Notify both players if online */
            player_t *requester_player = find_player_by_name(accounts[acc_requester].name);
            acceptor_player = find_player_by_name(accounts[acc_acceptor].name);
            if (acceptor_player) {
                protocol_create_message(&out, MSG_FRIEND_RESULT, "server", acceptor_player->name, "Friend added");
                protocol_send_message(acceptor_player->sock, &out);
            }
            if (requester_player) {
                char buf[BUF_SIZE];
                snprintf(buf, sizeof(buf), "%s accepted your friend request", msg.sender);
                protocol_create_message(&out, MSG_FRIEND_RESULT, "server", requester_player->name, buf);
                protocol_send_message(requester_player->sock, &out);
            }
            /* Remove the pending entry from acceptor */
            for (int p = 0; p < acceptor_player->num_pending_friend_requests; p++) {
                if (strcmp(acceptor_player->pending_friend_requests[p], msg.recipient) == 0) {
                    for (int q = p; q < acceptor_player->num_pending_friend_requests - 1; q++) {
                        strncpy(acceptor_player->pending_friend_requests[q], acceptor_player->pending_friend_requests[q+1], sizeof(acceptor_player->pending_friend_requests[q]));
                    }
                    acceptor_player->num_pending_friend_requests--;
                    break;
                }
            }
        }
            break;

        case MSG_FRIEND_REQUEST_REFUSE:
        {
            /* msg.sender = refuser, msg.recipient = original requester */
            message_t out;
            player_t *refuser = &players[player_index];
            /* Ensure there was a pending request from requester to this refuser */
            int foundr = 0;
            for (int p = 0; p < refuser->num_pending_friend_requests; p++) {
                if (strcmp(refuser->pending_friend_requests[p], msg.recipient) == 0) { foundr = 1; break; }
            }
            if (!foundr) {
                protocol_create_message(&out, MSG_FRIEND_RESULT, "server", msg.sender, "No pending friend request from this user");
                protocol_send_message(refuser->sock, &out);
                break;
            }
            player_t *requester_player = find_player_by_name(msg.recipient);
            if (requester_player) {
                char buf[BUF_SIZE];
                snprintf(buf, sizeof(buf), "%s refused your friend request", msg.sender);
                protocol_create_message(&out, MSG_FRIEND_RESULT, "server", msg.recipient, buf);
                protocol_send_message(requester_player->sock, &out);
            }
            /* Remove the pending entry from refuser */
            for (int p = 0; p < refuser->num_pending_friend_requests; p++) {
                if (strcmp(refuser->pending_friend_requests[p], msg.recipient) == 0) {
                    for (int q = p; q < refuser->num_pending_friend_requests - 1; q++) {
                        strncpy(refuser->pending_friend_requests[q], refuser->pending_friend_requests[q+1], sizeof(refuser->pending_friend_requests[q]));
                    }
                    refuser->num_pending_friend_requests--;
                    break;
                }
            }
            /* Acknowledge to the refuser */
            protocol_create_message(&out, MSG_FRIEND_RESULT, "server", msg.sender, "Friend request refused");
            protocol_send_message(players[player_index].sock, &out);
        }
            break;

        case MSG_REMOVE_FRIEND:
        {
            const char *torm = msg.data;
            int acc = find_account_index(players[player_index].name);
            message_t out;
            if (acc < 0) {
                protocol_create_message(&out, MSG_FRIEND_RESULT, "server", players[player_index].name, "Your account was not found. Try later.");
                protocol_send_message(players[player_index].sock, &out);
                break;
            }
            int target_acc = find_account_index(torm);
            if (target_acc < 0) {
                protocol_create_message(&out, MSG_FRIEND_RESULT, "server", players[player_index].name, "User not found");
                protocol_send_message(players[player_index].sock, &out);
                break;
            }
            if (!account_has_friend_idx(acc, target_acc)) {
                protocol_create_message(&out, MSG_FRIEND_RESULT, "server", players[player_index].name, "Not in your friends list");
                protocol_send_message(players[player_index].sock, &out);
                break;
            }
            // remove both sides
            if (account_remove_friend_idx(acc, target_acc) == 0 && account_remove_friend_idx(target_acc, acc) == 0) {
                protocol_create_message(&out, MSG_FRIEND_RESULT, "server", players[player_index].name, "Friend removed");
            } else {
                protocol_create_message(&out, MSG_FRIEND_RESULT, "server", players[player_index].name, "Failed to remove friend");
            }
            protocol_send_message(players[player_index].sock, &out);
        }
            break;
            
        case MSG_CHALLENGE:
            {
                printf("Received challenge from %s to %s\n", msg.sender, msg.recipient);
                /* Find the opponent */
                player_t *opponent = find_player_by_name(msg.recipient);
                
                if (!opponent) {
                    /* Player not found */
                    message_t error;
                    protocol_create_message(&error, MSG_ERROR, "server", msg.sender, "Player not found");
                    protocol_send_message(players[player_index].sock, &error);
                    break;
                }
                
                if (opponent == &(players[player_index])) {
                    message_t error;
                    protocol_create_message(&error, MSG_ERROR, "server", msg.sender, "You can't challenge yourself !");
                    protocol_send_message(players[player_index].sock, &error);
                    break;
                }
                
                /* Record pending challenger on the opponent so they can accept if challenged.
                   Keep a small list (avoid duplicates). */
                int exists = 0;
                for (int p = 0; p < opponent->num_pending_challengers; p++) {
                    if (strcmp(opponent->pending_challengers[p], msg.sender) == 0) { exists = 1; break; }
                }
                if (!exists && opponent->num_pending_challengers < MAX_PENDING_CHALLENGES) {
                    strncpy(opponent->pending_challengers[opponent->num_pending_challengers], msg.sender, sizeof(opponent->pending_challengers[0]) - 1);
                    opponent->pending_challengers[opponent->num_pending_challengers][sizeof(opponent->pending_challengers[0]) - 1] = '\0';
                    opponent->num_pending_challengers++;
                }
                /* Forward challenge to opponent */
                protocol_send_message(opponent->sock, &msg);
                printf("%s challenges %s\n", msg.sender, msg.recipient);
            }
            break;

        case MSG_CHALLENGE_ACCEPT:
        {
            player_t *acceptor = &players[player_index];
            player_t *challenger = find_player_by_name(msg.recipient);
            if (!challenger) {
                message_t error;
                protocol_create_message(&error, MSG_ERROR, "server", msg.sender, "Challenger not found");
                protocol_send_message(players[player_index].sock, &error);
                break;
            }
            /* Ensure the acceptor was actually challenged by this challenger (check the pending list) */
            int found = 0;
            for (int p = 0; p < acceptor->num_pending_challengers; p++) {
                if (strcmp(acceptor->pending_challengers[p], challenger->name) == 0) { found = 1; break; }
            }
            if (!found) {
                message_t error;
                protocol_create_message(&error, MSG_ERROR, "server", msg.sender, "No pending challenge from this player");
                protocol_send_message(acceptor->sock, &error);
                break;
            }

            /* Create session: challenger should be player0 (first argument) */
            int session_slot = session_create(challenger->name, challenger->sock, acceptor->name, acceptor->sock);
            if (session_slot == -1) {
                message_t error;
                char reason[BUF_SIZE];
                snprintf(reason, sizeof(reason), "There is no free session slot");
                protocol_create_message(&error, MSG_ERROR, "server", msg.sender, reason);
                protocol_send_message(acceptor->sock, &error);
                protocol_create_message(&error, MSG_ERROR, "server", msg.recipient, reason);
                protocol_send_message(challenger->sock, &error);
                break;
            }

            acceptor->in_game++;
            challenger->in_game++;

            /* Remove the challenger from the acceptor's pending list */
            for (int p = 0; p < acceptor->num_pending_challengers; p++) {
                if (strcmp(acceptor->pending_challengers[p], challenger->name) == 0) {
                    for (int q = p; q < acceptor->num_pending_challengers - 1; q++) {
                        strncpy(acceptor->pending_challengers[q], acceptor->pending_challengers[q+1], sizeof(acceptor->pending_challengers[q]));
                    }
                    acceptor->num_pending_challengers--;
                    break;
                }
            }
            /* Also remove any pending entry on challenger referencing acceptor (if present) */
            for (int p = 0; p < challenger->num_pending_challengers; p++) {
                if (strcmp(challenger->pending_challengers[p], acceptor->name) == 0) {
                    for (int q = p; q < challenger->num_pending_challengers - 1; q++) {
                        strncpy(challenger->pending_challengers[q], challenger->pending_challengers[q+1], sizeof(challenger->pending_challengers[q]));
                    }
                    challenger->num_pending_challengers--;
                    break;
                }
            }

            printf("%s accepted challenge from %s, session %d created\n", acceptor->name, challenger->name, session_slot);
        }
            break;

        case MSG_CHALLENGE_REFUSE:
        {
            player_t *challenger = find_player_by_name(msg.recipient);
            if (!challenger) {
                message_t error;
                protocol_create_message(&error, MSG_ERROR, "server", msg.sender, "Challenger not found");
                protocol_send_message(players[player_index].sock, &error);
                break;
            }

            /* ensure the player was actually challenged */
            int found = 0;

            /* Clear any pending challenge entries targeting this refuser (the player who refused) */
            player_t *refuser = &players[player_index];
            for (int p = 0; p < refuser->num_pending_challengers; p++) {
                if (strcmp(refuser->pending_challengers[p], challenger->name) == 0) {
                    for (int q = p; q < refuser->num_pending_challengers - 1; q++) {
                        strncpy(refuser->pending_challengers[q], refuser->pending_challengers[q+1], sizeof(refuser->pending_challengers[q]));
                    }
                    refuser->num_pending_challengers--;
                    found = 1;
                    break;
                }
            }

            if (!found) {
                message_t error;
                protocol_create_message(&error, MSG_ERROR, "server", msg.sender, "No pending challenge from this player");
                protocol_send_message(refuser->sock, &error);
                break;
            }

            message_t refuse_msg;
            char reason[BUF_SIZE];
            snprintf(reason, sizeof(reason), "%s refused your challenge", msg.sender);
            protocol_create_message(&refuse_msg, MSG_CHALLENGE_REFUSE, msg.sender, challenger->name, reason);
            protocol_send_message(challenger->sock, &refuse_msg);
            printf("%s refused the challenge from %s\n", msg.sender, challenger->name);
        }
            break;
            
        case MSG_PLAY_MOVE:
        {
            player_t player = players[player_index];

            int sid = -1;
            /* If recipient looks numeric, parse it as session id */
            if (msg.recipient[0] != '\0' && isdigit((unsigned char)msg.recipient[0])) {
                sid = atoi(msg.recipient);
            } else {
                message_t error;
                protocol_create_message(&error, MSG_ERROR, "server", msg.sender, "Invalid session id");
                protocol_send_message(player.sock, &error);
                printf("Did not send session chat from %s: %s because session id was invalid\n", msg.sender, msg.data);
                break;
            }

            /* Verify sender is part of session */
            char p1[64], p2[64];
            if (session_get_players(sid, p1, sizeof(p1), p2, sizeof(p2)) != 0) {
                message_t error;
                protocol_create_message(&error, MSG_ERROR, "server", msg.sender, "Invalid session id");
                protocol_send_message(player.sock, &error);
                break;
            }
            if (strcmp(p1, msg.sender) != 0 && strcmp(p2, msg.sender) != 0) {
                message_t error;
                protocol_create_message(&error, MSG_ERROR, "server", msg.sender, "You are not part of this session");
                protocol_send_message(player.sock, &error);
                break;
            }

            const char *opponent_name = session_get_opponent_name(sid, player.name);
            player_t *opponent = opponent_name ? find_player_by_name(opponent_name) : NULL;

            int move = atoi(msg.data);
            int flag = session_handle_move(sid, player.name, move);
            printf("Move handled for '%s' in session %d\n", player.name, sid);
            if (flag >= 0) session_broadcast_state(sid);

            // Check for game over
            if (flag == 1) {
                    printf("Session %d ended. Clearing in_game flags for '%s'%s\n",
                        sid, player.name, opponent ? opponent->name : "(unknown opponent)");
                    player.in_game--;
                    if (opponent) {
                        opponent->in_game--;
                    }
            }
        }
            break;

        case MSG_GIVE_UP:
        {
            /* Player wants to give up: find player and session, then handle give up */
            player_t player = players[player_index];

            int sid = -1;
            /* If recipient looks numeric, parse it as session id */
            if (msg.data[0] != '\0' && isdigit((unsigned char)msg.data[0])) {
                sid = atoi(msg.data);
            } else {
                message_t error;
                protocol_create_message(&error, MSG_ERROR, "server", msg.sender, "Invalid session id");
                protocol_send_message(player.sock, &error);
                break;
            }
            /* Verify sender is part of session */
            char p1[64], p2[64];
            if (session_get_players(sid, p1, sizeof(p1), p2, sizeof(p2)) != 0) {
                message_t error;
                protocol_create_message(&error, MSG_ERROR, "server", msg.sender, "Invalid session id");
                protocol_send_message(player.sock, &error);
                break;
            }
            if (strcmp(p1, msg.sender) != 0 && strcmp(p2, msg.sender) != 0) {
                message_t error;
                protocol_create_message(&error, MSG_ERROR, "server", msg.sender, "You are not part of this session");
                protocol_send_message(player.sock, &error);
                break;
            }

            /* Determine opponent name before session is destroyed */
            const char *opponent_name = session_get_opponent_name(sid, player.name);
            player_t *opponent = opponent_name ? find_player_by_name(opponent_name) : NULL;

            /* Perform give up inside session module */
            if (session_give_up(sid, player.name) == 0) {
                /* Clear player in_game flags for both participants */
                player.in_game--;
                if (opponent) { opponent->in_game--; }
            } else {
                message_t error;
                protocol_create_message(&error, MSG_ERROR, "server", msg.sender, "Failed to process give up");
                protocol_send_message(player.sock, &error);
            }
            printf("%s gave up the game\n", player.name);
        }
            break;
            
        case MSG_PRIVATE_CHAT:
        {
            /* If recipient matches an online player name, treat as private chat */
            player_t *target = find_player_by_name(msg.recipient);
            if (target) {
                message_t chat;
                protocol_create_private_chat(&chat, msg.sender, msg.recipient, msg.data);
                protocol_send_message(target->sock, &chat);
                printf("Private message from %s to %s\n", msg.sender, msg.recipient);
            } else {
                message_t error;
                protocol_create_message(&error, MSG_ERROR, "server", msg.sender, "No online player with that name");
                protocol_send_message(players[player_index].sock, &error);
                printf("Private message from %s to unknown recipient %s\n", msg.sender, msg.recipient);
            }
        }
            break;

        case MSG_SESSION_CHAT:
        {
            player_t player = players[player_index];

            int sid = -1;
            /* If recipient looks numeric, parse it as session id */
            if (msg.recipient[0] != '\0' && isdigit((unsigned char)msg.recipient[0])) {
                sid = atoi(msg.recipient);
            } else {
                message_t error;
                protocol_create_message(&error, MSG_ERROR, "server", msg.sender, "Invalid session id");
                protocol_send_message(player.sock, &error);
                printf("Did not send session chat from %s: %s because session id was invalid\n", msg.sender, msg.data);
                break;
            }

            /* Verify sender is part of session */
            char p1[64], p2[64];
            if (session_get_players(sid, p1, sizeof(p1), p2, sizeof(p2)) != 0) {
                message_t error;
                protocol_create_message(&error, MSG_ERROR, "server", msg.sender, "Invalid session id");
                protocol_send_message(player.sock, &error);
                break;
            }
            if (strcmp(p1, msg.sender) != 0 && strcmp(p2, msg.sender) != 0) {
                message_t error;
                protocol_create_message(&error, MSG_ERROR, "server", msg.sender, "Only participants can send session chat");
                protocol_send_message(player.sock, &error);
                break;
            }

            /* Build chat message and send to opponent */
            message_t chat;
            char sid_str[32];
            snprintf(sid_str, sizeof(sid_str), "%d", sid);
            protocol_create_private_chat(&chat, msg.sender, sid_str, msg.data);

            const char *opponent_name = session_get_opponent_name(sid, msg.sender);
            player_t *opponent = opponent_name ? find_player_by_name(opponent_name) : NULL;
            if (opponent) {
                protocol_send_message(opponent->sock, &chat);
            }
        }
            break;

        case MSG_SPECTATE:
        {
            /* Recipient should contain the session id to observe (as string). If empty, try data. */
            int sid = -1;
            if (msg.recipient[0] != '\0') sid = atoi(msg.recipient);
            else if (msg.data[0] != '\0') sid = atoi(msg.data);

            if (sid < 0) {
                message_t error;
                protocol_create_message(&error, MSG_ERROR, "server", players[player_index].name, "Invalid session id");
                protocol_send_message(players[player_index].sock, &error);
                break;
            }
            /* Privacy checks: retrieve the two players in the session and verify their private flags.
             * If none are private -> allowed. If some are private, the spectator must be friend with at
             * least one private player. */
            char p1[64], p2[64];
            if (session_get_players(sid, p1, sizeof(p1), p2, sizeof(p2)) != 0) {
                message_t error;
                protocol_create_message(&error, MSG_ERROR, "server", players[player_index].name, "Invalid session id");
                protocol_send_message(players[player_index].sock, &error);
                break;
            }

            player_t *player_a = find_player_by_name(p1);
            player_t *player_b = find_player_by_name(p2);

            int private_a = player_a ? player_a->private_mode : 0;
            int private_b = player_b ? player_b->private_mode : 0;

            int acc_spectator = find_account_index(players[player_index].name);
            int allowed = 0;

            if (!private_a && !private_b) {
                allowed = 1; /* public game */
            } else {
                /* If either side is private, spectator must be friend with at least one private player */
                if (acc_spectator >= 0) {
                    if (private_a) {
                        int acc_a = find_account_index(p1);
                        if (acc_a >= 0 && account_has_friend_idx(acc_a, acc_spectator)) allowed = 1;
                    }
                    if (!allowed && private_b) {
                        int acc_b = find_account_index(p2);
                        if (acc_b >= 0 && account_has_friend_idx(acc_b, acc_spectator)) allowed = 1;
                    }
                }
            }

            if (!allowed) {
                message_t error;
                protocol_create_message(&error, MSG_ERROR, "server", players[player_index].name, "Cannot spectate: one or more players set their game to private");
                protocol_send_message(players[player_index].sock, &error);
                break;
            }

            /* Allowed -> add observer and notify */
            if (session_add_observer(sid, players[player_index].name, players[player_index].sock) == 0) {
                message_t ok;
                protocol_create_message(&ok, MSG_SPECTATE, "server", players[player_index].name, "Now observing session");
                protocol_send_message(players[player_index].sock, &ok);

                /* Server terminal notice */
                printf("%s is now spectating session %d (%s vs %s)\n", players[player_index].name, sid, p1, p2);

                /* Notify the two players in the game that someone is observing */
                char notice[BUF_SIZE];
                snprintf(notice, sizeof(notice), "%s is observing your game", players[player_index].name);
                message_t nmsg;
                char senderName[64];
                snprintf(senderName, sizeof(senderName), "Session %d", sid);
                protocol_create_message(&nmsg, MSG_PRIVATE_CHAT, senderName, p1, notice);
                if (player_a) protocol_send_message(player_a->sock, &nmsg);
                protocol_create_message(&nmsg, MSG_PRIVATE_CHAT, senderName, p2, notice);
                if (player_b) protocol_send_message(player_b->sock, &nmsg);
            } else {
                message_t error;
                protocol_create_message(&error, MSG_ERROR, "server", players[player_index].name, "Failed to observe session");
                protocol_send_message(players[player_index].sock, &error);
            }
        }
            break;

        case MSG_SET_PRIVATE:
        {
            /* msg.data: "1" to enable, "0" to disable, or "toggle" to flip */
            int newval = -1;
            if (strcmp(msg.data, "toggle") == 0) {
                players[player_index].private_mode = !players[player_index].private_mode;
                newval = players[player_index].private_mode;
            } else if (msg.data[0] == '1') {
                players[player_index].private_mode = 1;
                newval = 1;
            } else if (msg.data[0] == '0') {
                players[player_index].private_mode = 0;
                newval = 0;
            }

            message_t out;
            if (newval == 1) {
                protocol_create_message(&out, MSG_FRIEND_RESULT, "server", players[player_index].name, "Private mode enabled");
            } else if (newval == 0) {
                protocol_create_message(&out, MSG_FRIEND_RESULT, "server", players[player_index].name, "Private mode disabled");
            } else {
                protocol_create_message(&out, MSG_FRIEND_RESULT, "server", players[player_index].name, "Unknown parameter for private command (use '1','0' or 'toggle')");
            }
            protocol_send_message(players[player_index].sock, &out);
        }
            break;

        case MSG_BIO_VIEW:
        {
            player_t *player = find_player_by_name(msg.recipient);
            if (!player) {
                message_t error;
                char reason[BUF_SIZE];
                snprintf(reason, sizeof(reason), "%s is not a player !", msg.recipient);
                protocol_create_message(&error, MSG_ERROR, "server", msg.sender, reason);
                protocol_send_message(players[player_index].sock, &error);
                break;
            }
            message_t bio;
            protocol_create_message(&bio, MSG_BIO_VIEW, msg.recipient, msg.sender, player->bio);
            protocol_send_message(players[player_index].sock, &bio);
        }
            break;

        case MSG_BIO_EDIT:
        {
            /* Update in-memory player bio and persist to account store if exists */
            strncpy(players[player_index].bio, msg.data, sizeof(players[player_index].bio) - 1);
            players[player_index].bio[sizeof(players[player_index].bio) - 1] = '\0';
            int acc = find_account_index(players[player_index].name);
            if (acc >= 0) {
                /* Update account bio and save */
                strncpy(accounts[acc].bio, players[player_index].bio, sizeof(accounts[acc].bio) - 1);
                accounts[acc].bio[sizeof(accounts[acc].bio) - 1] = '\0';
                save_accounts();
            }
        }
            break;
        

        default:
            fprintf(stderr, "Unknown message type: %d\n", msg.type);
            break;
    }
}

// Add a connected player to the in-memory players list.
static int add_player(SOCKET sock, const char *name)
{
    if (num_players >= MAX_PLAYERS) {
        return -1;
    }
    
    /* Check for duplicate name (already online) */
    if (find_player_by_name(name) != NULL) {
        return -1;
    }
    
    players[num_players].sock = sock;
    strncpy(players[num_players].name, name, sizeof(players[num_players].name) - 1);
    players[num_players].in_game = 0;
    players[num_players].private_mode = 0;
    players[num_players].num_pending_challengers = 0;
    players[num_players].num_pending_friend_requests = 0;
    for (int p = 0; p < MAX_PENDING_CHALLENGES; p++) players[num_players].pending_challengers[p][0] = '\0';
    for (int p = 0; p < MAX_PENDING_CHALLENGES; p++) players[num_players].pending_friend_requests[p][0] = '\0';
    /* Copy bio from account store if available */
    int acc = find_account_index(name);
    if (acc >= 0) {
        printf("Loading bio for player '%s'\n", name);
        strncpy(players[num_players].bio, accounts[acc].bio, sizeof(players[num_players].bio) - 1);
        players[num_players].bio[sizeof(players[num_players].bio) - 1] = '\0';
    } else {
        players[num_players].bio[0] = '\0';
    }
    
    num_players++;
    return num_players - 1;
}

// Remove a player from the in-memory list and cleanup observers.
static void remove_player(int index)
{
    if (index < 0 || index >= num_players) {
        return;
    }
    
    /* Remove the player as an observer from any sessions */
    SOCKET sock = players[index].sock;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        session_remove_observer(i, sock);
    }

    /* Shift remaining players */
    for (int i = index; i < num_players - 1; i++) {
        players[i] = players[i + 1];
    }
    
    num_players--;
}

// Lookup a connected player by name and return pointer or NULL.
static player_t* find_player_by_name(const char *name)
{
    for (int i = 0; i < num_players; i++) {
        if (strcmp(players[i].name, name) == 0) {
            return &players[i];
        }
    }
    return NULL;
}
