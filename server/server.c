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

#define MAX_PLAYERS 100

typedef struct {
    SOCKET sock;
    char name[64];
    int in_game;
    int player_index; 
    char bio[BUF_SIZE];
} player_t;

static player_t players[MAX_PLAYERS];
static int num_players = 0;

/* Function prototypes */
static void init_server(void);
static void cleanup_server(void);
static void run_server(void);
static int add_player(SOCKET sock, const char *name);
static void remove_player(int index);
static player_t* find_player_by_name(const char *name);
static void handle_new_connection(SOCKET server_sock);
static void handle_client_message(int player_index);
static void broadcast_player_list(void);

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
        if (add_player(client_sock, msg.sender) >= 0) {
            printf("Player '%s' logged in\n", msg.sender);
            broadcast_player_list();
        } else {
            fprintf(stderr, "Failed to add player '%s'\n", msg.sender);
            net_close(client_sock);
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
        net_close(players[player_index].sock);
        remove_player(player_index);
        broadcast_player_list();
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
            
        case MSG_CHALLENGE:
            {
                player_t *opponent = find_player_by_name(msg.recipient);
                
                if (!opponent) {
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
                    message_t error;
                    protocol_create_message(&error, MSG_ERROR, "server", msg.sender, "Player is already in a game");
                    protocol_send_message(players[player_index].sock, &error);
                    break;
                }
                
                if (players[player_index].in_game) {
                    message_t error;
                    protocol_create_message(&error, MSG_ERROR, "server", msg.sender, "You are already in a game");
                    protocol_send_message(players[player_index].sock, &error);
                    break;
                }
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
            player_t *opponent = find_player_by_name(opponent_name);

            session_handle_move(sid, player->name, atoi(msg.data));
            session_broadcast_state(sid);

            // Check for game over
            if (session_find_by_player(player->name) == -1) {
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
                broadcast_player_list();
            } else {
                message_t error;
                protocol_create_message(&error, MSG_ERROR, "server", msg.sender, "Failed to process give up");
                protocol_send_message(player->sock, &error);
            }
        }
            break;
            
        case MSG_CHAT:
        {
            player_t *target = find_player_by_name(msg.recipient);
            if (target) {
                message_t chat;
                protocol_create_chat(&chat, msg.sender, msg.recipient, msg.data);
                protocol_send_message(target->sock, &chat);
            } 
            else {
                message_t chat;
                protocol_create_chat(&chat, msg.sender, "", strcat(strcat(msg.recipient, " "), msg.data));
                for (int i = 0; i < num_players; i++) {
                    protocol_send_message(players[i].sock, &chat);
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
            strncpy(players[player_index].bio, msg.data, sizeof(players[player_index].bio) - 1);
            players[player_index].bio[sizeof(players[player_index].bio) - 1] = '\0';
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
    
    /* Check for duplicate name */
    if (find_player_by_name(name) != NULL) {
        return -1;
    }
    
    players[num_players].sock = sock;
    strncpy(players[num_players].name, name, sizeof(players[num_players].name) - 1);
    players[num_players].in_game = 0;
    players[num_players].player_index = -1;
    
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

static void broadcast_player_list(void)
{
    /* Format player names into a message and broadcast */
}
