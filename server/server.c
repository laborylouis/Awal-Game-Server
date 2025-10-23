#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/select.h>

#include "../common/net.h"
#include "../common/protocol.h"
#include "../game/awale.h"

#define MAX_PLAYERS 100

typedef struct {
    SOCKET sock;
    char name[64];
    int in_game;
    int player_index; /* 0 or 1 in current game */
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

int main(int argc, char **argv)
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
        /* Client disconnected */
        printf("Player '%s' disconnected\n", players[player_index].name);
        net_close(players[player_index].sock);
        remove_player(player_index);
        broadcast_player_list();
        return;
    }
    
    /* Handle different message types */
    switch (msg.type) {
        case MSG_LIST_PLAYERS:
            /* Send player list to requesting client */
            /* TODO: Implement */
            break;
            
        case MSG_CHALLENGE:
            /* Handle challenge request */
            /* TODO: Implement */
            break;
            
        case MSG_PLAY_MOVE:
            /* Handle game move */
            /* TODO: Implement */
            break;
            
        case MSG_CHAT:
            /* Handle chat message */
            /* TODO: Implement */
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
    /* TODO: Send list of online players to all connected clients */
    /* Format player names into a message and broadcast */
}
