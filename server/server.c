#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/select.h>
#include <time.h>

#include "../common/net.h"
#include "../common/protocol.h"
#include "../game/awale.h"
#include "session.h"

#define MAX_PLAYERS 100 // Maximum connected players

typedef struct {
    SOCKET sock;
    char name[64];
    int in_game;
    int player_index; /* 0 or 1 in current game */
    char bio[BUF_SIZE];
} player_t;

static player_t players[MAX_PLAYERS];
static int num_players = 0;

/* ***** Account storage (persistent) ***** */

#define ACCOUNTS_FILE "accounts.db"
#define MAX_ACCOUNTS 1000 // Maximum number of accounts stored
#define MAX_PASSWORD_HASH_LENGTH 65 // Length for password hash strings
typedef struct {
    char name[64];
    char hash[MAX_PASSWORD_HASH_LENGTH];
    char bio[BUF_SIZE];
} account_t;
static account_t accounts[MAX_ACCOUNTS];
static int num_accounts = 0;

static void load_accounts(void);
static int find_account_index(const char *name);
static int add_account(const char *name, const char *hash, const char *bio);
static int save_accounts(void);
static void escape_string(const char *in, char *out, int out_size);
static void unescape_string(const char *in, char *out, int out_size);

/* Function prototypes */
static void init_server(void);
static void cleanup_server(void);
static void run_server(void);
static int add_player(SOCKET sock, const char *name);
static void remove_player(int index);
static player_t* find_player_by_name(const char *name);
static void handle_new_connection(SOCKET server_sock);
static void handle_client_message(int player_index);

// Password hashing function
void hash_password(const char *password, char *hashed_password);

/* Account store helpers */
static void load_accounts(void)
{
    num_accounts = 0;
    FILE *f = fopen(ACCOUNTS_FILE, "r");
    if (!f) return;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n'); if (nl) *nl = '\0';
        /* Format: name|hash|bio_escaped */
        char *p1 = strchr(line, '|');
        if (!p1) continue;
        *p1 = '\0';
        char *p2 = strchr(p1 + 1, '|');
        if (!p2) continue;
        *p2 = '\0';
        char *name = line;
        char *hash = p1 + 1;
        char *bio_esc = p2 + 1;
        if (num_accounts < MAX_ACCOUNTS) {
            strncpy(accounts[num_accounts].name, name, sizeof(accounts[num_accounts].name)-1);
            accounts[num_accounts].name[sizeof(accounts[num_accounts].name)-1] = '\0';
            strncpy(accounts[num_accounts].hash, hash, sizeof(accounts[num_accounts].hash)-1);
            accounts[num_accounts].hash[sizeof(accounts[num_accounts].hash)-1] = '\0';
            /* Unescape bio */
            unescape_string(bio_esc, accounts[num_accounts].bio, sizeof(accounts[num_accounts].bio));
            num_accounts++;
        }
    }
    fclose(f);
}

static int find_account_index(const char *name)
{
    for (int i = 0; i < num_accounts; i++) {
        if (strcmp(accounts[i].name, name) == 0) return i;
    }
    return -1;
}

static int add_account(const char *name, const char *hash, const char *bio)
{
    if (num_accounts >= MAX_ACCOUNTS) return -1;
    /* Append to file */
    /* Add to in-memory list */
    strncpy(accounts[num_accounts].name, name, sizeof(accounts[num_accounts].name)-1);
    accounts[num_accounts].name[sizeof(accounts[num_accounts].name)-1] = '\0';
    strncpy(accounts[num_accounts].hash, hash, sizeof(accounts[num_accounts].hash)-1);
    accounts[num_accounts].hash[sizeof(accounts[num_accounts].hash)-1] = '\0';
    if (bio) strncpy(accounts[num_accounts].bio, bio, sizeof(accounts[num_accounts].bio)-1);
    else accounts[num_accounts].bio[0] = '\0';
    accounts[num_accounts].bio[sizeof(accounts[num_accounts].bio)-1] = '\0';
    num_accounts++;

    /* Persist all accounts (append not reliable when editing bios) */
    if (save_accounts() != 0) return -1;
    return 0;
}

/* Save all accounts to ACCOUNTS_FILE using escaped bios */
static int save_accounts(void)
{
    FILE *f = fopen(ACCOUNTS_FILE, "w");
    if (!f) return -1;
    char bio_esc[BUF_SIZE * 2];
    for (int i = 0; i < num_accounts; i++) {
        escape_string(accounts[i].bio, bio_esc, sizeof(bio_esc));
        fprintf(f, "%s|%s|%s\n", accounts[i].name, accounts[i].hash, bio_esc);
    }
    fclose(f);
    return 0;
}

/* Escape newline and pipe characters for safe storage in a single-line DB format */
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

int main()
{
    printf("=== Awale Game Server ===\n");
    printf("Initializing...\n");
    
    init_server();
    run_server();
    cleanup_server();
    
    return EXIT_SUCCESS;
}

static void init_server(void)
{
    net_init();
    sessions_init();
    load_accounts();
    /* Seed RNG once at startup so rand() produces varied results */
    srand((unsigned)time(NULL));
    memset(players, 0, sizeof(players));
    num_players = 0;
}

static void cleanup_server(void)
{
    /* Close all client connections */
    for (int i = 0; i < num_players; i++) {
        net_close(players[i].sock);
    }
    
    net_cleanup();
}

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
        
        /* Add all connected players */
        for (int i = 0; i < num_players; i++) {
            FD_SET(players[i].sock, &readfds);
            if (players[i].sock > max_fd) {
                max_fd = players[i].sock;
            }
        }
        
        /* Wait for activity */
        if (select(max_fd + 1, &readfds, NULL, NULL, NULL) < 0) {
            perror("select");
            break;
        }
        
        /* Check for new connection */
        if (FD_ISSET(server_sock, &readfds)) {
            handle_new_connection(server_sock);
        }
        
        /* Check existing clients */
        for (int i = 0; i < num_players; i++) {
            if (FD_ISSET(players[i].sock, &readfds)) {
                handle_client_message(i);
            }
        }
    }
    
    net_close(server_sock);
}

static void handle_new_connection(SOCKET server_sock)
{
    SOCKADDR_IN client_addr;
    SOCKET client_sock = net_accept_connection(server_sock, &client_addr);
    
    if (client_sock == INVALID_SOCKET) {
        return;
    }
    
    printf("New connection from %s\n", inet_ntoa(client_addr.sin_addr));
    
    /* Wait for login message */
    message_t msg;
    if (protocol_recv_message(client_sock, &msg) < 0) {
        net_close(client_sock);
        return;
    }
    
    if (msg.type == MSG_LOGIN) {
        /* msg.sender = username, msg.data = password (plain). Server hashes and verifies or registers account. */
        const char *username = msg.sender;
        const char *password = msg.data;

        int acc = find_account_index(username);
        if (acc >= 0) {
            /* Account exists: verify password */
            if (strcmp(accounts[acc].hash, password) != 0) {
                message_t err;
                protocol_create_message(&err, MSG_ERROR, "server", username, "Invalid password");
                protocol_send_message(client_sock, &err);
                net_close(client_sock);
            } else {
                /* Password OK. Ensure user not already online */
                if (find_player_by_name(username) != NULL) {
                    message_t err;
                    protocol_create_message(&err, MSG_ERROR, "server", username, "User already online");
                    protocol_send_message(client_sock, &err);
                    net_close(client_sock);
                } else {
                    int idx = add_player(client_sock, username);
                    if (idx >= 0) {
                        printf("Player '%s' logged in\n", username);
                        /* Notify client that login succeeded */
                        message_t ok_msg;
                        protocol_create_chat(&ok_msg, "server", username, "Login successful. Welcome!");
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
            /* New account: register and add player */
            if (add_account(username, password, "") != 0) {
                message_t err;
                protocol_create_message(&err, MSG_ERROR, "server", username, "Failed to register account");
                protocol_send_message(client_sock, &err);
                net_close(client_sock);
            } else {
                int idx = add_player(client_sock, username);
                if (idx >= 0) {
                    printf("Registered and logged in new player '%s'\n", username);
                    /* Notify client that registration and login succeeded */
                    message_t ok_msg;
                    protocol_create_chat(&ok_msg, "server", username, "Account created and logged in. Welcome!");
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

static void handle_client_message(int player_index)
{
    message_t msg;
    int result = protocol_recv_message(players[player_index].sock, &msg);
    
    if (result <= 0) {
        printf("Player '%s' disconnected\n", players[player_index].name);
        /* If the player was in a game, ensure the session is cleaned up and the opponent's
           in_game flag is cleared so they are not left marked as in-game. */
        if (players[player_index].in_game) {
            /* Find session and opponent */
            int sid = session_find_by_player(players[player_index].name);
            const char *opponent_name = (sid >= 0) ? session_get_opponent_name(sid, players[player_index].name) : NULL;
            player_t *opponent = opponent_name ? find_player_by_name(opponent_name) : NULL;

            /* If a session exists, perform give-up to finalize and destroy the session. */
            if (sid >= 0) {
                session_give_up(sid, players[player_index].name);
            }

            /* Clear opponent flags if opponent is present in players[] */
            if (opponent) {
                opponent->in_game = 0;
                opponent->player_index = -1;
            }
        }

        net_close(players[player_index].sock);
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
            /* Return list of active sessions */
            char list[BUF_SIZE];
            session_list_games(list, sizeof(list));
            message_t out;
            protocol_create_message(&out, MSG_GAME_LIST, "server", players[player_index].name, list);
            protocol_send_message(players[player_index].sock, &out);
        }
            break;
            
        case MSG_CHALLENGE:
            {
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

                if (opponent->in_game) {
                    /* Player already in a game */
                    message_t error;
                    protocol_create_message(&error, MSG_ERROR, "server", msg.sender, "Player is already in a game");
                    protocol_send_message(players[player_index].sock, &error);
                    break;
                }
                
                if (players[player_index].in_game) {
                    /* Challenger already in a game */
                    message_t error;
                    protocol_create_message(&error, MSG_ERROR, "server", msg.sender, "You are already in a game");
                    protocol_send_message(players[player_index].sock, &error);
                    break;
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
            if (challenger->in_game) {
                message_t error;
                char reason[BUF_SIZE];
                snprintf(reason, sizeof(reason), "%s is already in a game", challenger->name);
                protocol_create_message(&error, MSG_ERROR, "server", msg.sender, reason);
                protocol_send_message(players[player_index].sock, &error);
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

            acceptor->in_game = 1;
            challenger->in_game = 1;
            acceptor->player_index = 1;
            challenger->player_index = 0;

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
            player_t *player = find_player_by_name(msg.sender);
            if (!player) {
            message_t error;
            protocol_create_message(&error, MSG_ERROR, "server", msg.sender, "Player not found");
            protocol_send_message(players[player_index].sock, &error);
            break;
            }

            if (!player->in_game) {
            message_t error;
            protocol_create_message(&error, MSG_ERROR, "server", msg.sender, "You're not in game !");
            protocol_send_message(player->sock, &error);
            break;
            }

            int sid = session_find_by_player(player->name);
            if (sid < 0) {
            message_t error;
            protocol_create_message(&error, MSG_ERROR, "server", msg.sender, "No active session");
            protocol_send_message(player->sock, &error);
            break;
            }

            const char *opponent_name = session_get_opponent_name(sid, player->name);
            player_t *opponent = opponent_name ? find_player_by_name(opponent_name) : NULL;

            int move = atoi(msg.data);
            session_handle_move(sid, player->name, move);
            printf("Move handled for '%s' in session %d\n", player->name, sid);
            session_broadcast_state(sid);

            // Check for game over
            if (session_find_by_player(player->name) == -1) {
                printf("Session %d ended. Clearing in_game flags for '%s'%s\n",
                    sid, player->name, opponent ? "" : " (no opponent found)");
                if (player) {
                    player->in_game = 0;
                    player->player_index = -1;
                }
                if (opponent) {
                    opponent->in_game = 0;
                    opponent->player_index = -1;
                }
            }
        }
            break;

        case MSG_GIVE_UP:
        {
            /* Player wants to give up: find player and session, then handle give up */
            player_t *player = find_player_by_name(msg.sender);
            if (!player) {
                message_t error;
                protocol_create_message(&error, MSG_ERROR, "server", msg.sender, "Player not found");
                protocol_send_message(players[player_index].sock, &error);
                break;
            }
            if (!player->in_game) {
                message_t error;
                protocol_create_message(&error, MSG_ERROR, "server", msg.sender, "You are not in a game");
                protocol_send_message(player->sock, &error);
                break;
            }
            int sid = session_find_by_player(player->name);
            if (sid < 0) {
                message_t error;
                protocol_create_message(&error, MSG_ERROR, "server", msg.sender, "No active session");
                protocol_send_message(player->sock, &error);
                break;
            }

            /* Determine opponent name before session is destroyed */
            const char *opponent_name = session_get_opponent_name(sid, player->name);
            player_t *opponent = opponent_name ? find_player_by_name(opponent_name) : NULL;

            /* Perform give up inside session module */
            if (session_give_up(sid, player->name) == 0) {
                /* Clear player in_game flags for both participants */
                if (player) { player->in_game = 0; player->player_index = -1; }
                if (opponent) { opponent->in_game = 0; opponent->player_index = -1; }
            } else {
                message_t error;
                protocol_create_message(&error, MSG_ERROR, "server", msg.sender, "Failed to process give up");
                protocol_send_message(player->sock, &error);
            }
            printf("%s gave up the game\n", player->name);
        }
            break;
            
        case MSG_CHAT:
        {
            player_t *target = find_player_by_name(msg.recipient);
            if (target) {
                // private chat
                message_t chat;
                protocol_create_chat(&chat, msg.sender, msg.recipient, msg.data);
                protocol_send_message(target->sock, &chat);
            }
            else {
                // session chat
                // if the sender is not in a game send an error
                player_t *sender = find_player_by_name(msg.sender);
                if (!sender->in_game) {
                    message_t error;
                    protocol_create_message(&error, MSG_ERROR, "server", msg.sender, "You must be in a game to send session chat! Type 'help' for more info.");
                    protocol_send_message(players[player_index].sock, &error);
                    break;
                }
                message_t chat;
                protocol_create_chat(&chat, msg.sender, "", strcat(strcat(msg.recipient, " "), msg.data));

                int sid = session_find_by_player(sender->name);
                if (sid < 0) {
                    message_t error;
                    protocol_create_message(&error, MSG_ERROR, "server", msg.sender, "No active session");
                    protocol_send_message(sender->sock, &error);
                    break;
                }
                const char *opponent_name = session_get_opponent_name(sid, sender->name);
                player_t *opponent = find_player_by_name(opponent_name);
                if (opponent) {
                    protocol_send_message(opponent->sock, &chat);
                }
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

            if (session_add_observer(sid, players[player_index].name, players[player_index].sock) == 0) {
                message_t ok;
                protocol_create_message(&ok, MSG_SPECTATE, "server", players[player_index].name, "Now observing session");
                protocol_send_message(players[player_index].sock, &ok);
            } else {
                message_t error;
                protocol_create_message(&error, MSG_ERROR, "server", players[player_index].name, "Failed to observe session");
                protocol_send_message(players[player_index].sock, &error);
            }
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
    players[num_players].player_index = -1;
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

static player_t* find_player_by_name(const char *name)
{
    for (int i = 0; i < num_players; i++) {
        if (strcmp(players[i].name, name) == 0) {
            return &players[i];
        }
    }
    return NULL;
}

