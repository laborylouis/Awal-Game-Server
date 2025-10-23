#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#include "../common/net.h"
#include "../common/protocol.h"

/* Client state */
static SOCKET server_sock = INVALID_SOCKET;
static char username[64];
static int in_game = 0;

/* Function prototypes */
static void init_client(void);
static void cleanup_client(void);
static int connect_to_server(const char *host, int port);
static void run_client_loop(void);
static void handle_user_input(void);
static void handle_server_message(void);
static void print_help(void);

int main(int argc, char **argv)
{
    const char *server_host = "127.0.0.1";
    int server_port = DEFAULT_PORT;
    
    printf("=== Awale Game Client ===\n");
    
    if (argc > 1) {
        server_host = argv[1];
    }
    if (argc > 2) {
        server_port = atoi(argv[2]);
    }
    
    init_client();
    
    /* Get username */
    printf("Enter your username: ");
    fflush(stdout);
    if (fgets(username, sizeof(username), stdin) == NULL) {
        cleanup_client();
        return EXIT_FAILURE;
    }
    /* Remove newline */
    username[strcspn(username, "\n")] = '\0';
    
    if (strlen(username) == 0) {
        fprintf(stderr, "Username cannot be empty\n");
        cleanup_client();
        return EXIT_FAILURE;
    }
    
    /* Connect to server */
    if (connect_to_server(server_host, server_port) < 0) {
        cleanup_client();
        return EXIT_FAILURE;
    }
    
    printf("Connected to server at %s:%d\n", server_host, server_port);
    printf("Type 'help' for available commands\n\n");
    
    /* Send login message */
    message_t login_msg;
    protocol_create_login(&login_msg, username);
    if (protocol_send_message(server_sock, &login_msg) < 0) {
        fprintf(stderr, "Failed to send login message\n");
        cleanup_client();
        return EXIT_FAILURE;
    }
    
    /* Main client loop */
    run_client_loop();
    
    cleanup_client();
    return EXIT_SUCCESS;
}

static void init_client(void)
{
    net_init();
    in_game = 0;
}

static void cleanup_client(void)
{
    if (server_sock != INVALID_SOCKET) {
        net_close(server_sock);
    }
    net_cleanup();
}

static int connect_to_server(const char *host, int port)
{
    server_sock = net_create_socket();
    if (server_sock == INVALID_SOCKET) {
        fprintf(stderr, "Failed to create socket\n");
        return -1;
    }
    
    if (net_connect(server_sock, host, port) < 0) {
        fprintf(stderr, "Failed to connect to %s:%d\n", host, port);
        return -1;
    }
    
    return 0;
}

static void run_client_loop(void)
{
    fd_set readfds;
    
    while (1) {
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(server_sock, &readfds);
        
        int max_fd = (server_sock > STDIN_FILENO) ? server_sock : STDIN_FILENO;
        
        if (select(max_fd + 1, &readfds, NULL, NULL, NULL) < 0) {
            perror("select");
            break;
        }
        
        /* Handle user input */
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            handle_user_input();
        }
        
        /* Handle server messages */
        if (FD_ISSET(server_sock, &readfds)) {
            handle_server_message();
        }
    }
}

static void handle_user_input(void)
{
    char input[BUF_SIZE];
    if (fgets(input, sizeof(input), stdin) == NULL) {
        return;
    }
    
    /* Remove newline */
    input[strcspn(input, "\n")] = '\0';
    
    if (strlen(input) == 0) {
        return;
    }
    
    /* Parse commands */
    if (strcmp(input, "help") == 0) {
        print_help();
    }
    else if (strcmp(input, "list") == 0) {
        /* Request player list */
        message_t msg;
        protocol_create_message(&msg, MSG_LIST_PLAYERS, username, "", "");
        protocol_send_message(server_sock, &msg);
    }
    else if (strncmp(input, "challenge ", 10) == 0) {
        /* Challenge another player */
        char *opponent = input + 10;
        message_t msg;
        protocol_create_challenge(&msg, username, opponent);
        protocol_send_message(server_sock, &msg);
        printf("Challenge sent to %s\n", opponent);
    }
    else if (strncmp(input, "move ", 5) == 0) {
        /* Play a move */
        if (!in_game) {
            printf("You are not in a game\n");
            return;
        }
        int hole = atoi(input + 5);
        message_t msg;
        protocol_create_move(&msg, username, hole);
        protocol_send_message(server_sock, &msg);
    }
    else if (strncmp(input, "chat ", 5) == 0) {
        /* Send chat message */
        message_t msg;
        protocol_create_chat(&msg, username, "", input + 5);
        protocol_send_message(server_sock, &msg);
    }
    else if (strcmp(input, "quit") == 0) {
        printf("Disconnecting...\n");
        exit(0);
    }
    else {
        printf("Unknown command. Type 'help' for available commands.\n");
    }
}

static void handle_server_message(void)
{
    message_t msg;
    int result = protocol_recv_message(server_sock, &msg);
    
    if (result <= 0) {
        printf("Server disconnected\n");
        exit(0);
    }
    
    /* Handle different message types */
    switch (msg.type) {
        case MSG_GAME_START:
            printf("\n=== Game starting against %s ===\n", msg.data);
            in_game = 1;
            break;
            
        case MSG_GAME_STATE:
            printf("%s\n", msg.data);
            break;
            
        case MSG_GAME_OVER:
            printf("\n%s\n", msg.data);
            in_game = 0;
            break;
            
        case MSG_PLAYER_LIST:
            printf("Online players:\n%s\n", msg.data);
            break;
            
        case MSG_CHALLENGE:
            printf("\n>>> %s challenges you to a game! <<<\n", msg.sender);
            printf("Type 'accept %s' or 'refuse %s'\n", msg.sender, msg.sender);
            break;
            
        case MSG_CHAT:
            printf("[%s]: %s\n", msg.sender, msg.data);
            break;
            
        case MSG_ERROR:
            printf("Error: %s\n", msg.data);
            break;
            
        default:
            printf("Received unknown message type: %d\n", msg.type);
            break;
    }
}

static void print_help(void)
{
    printf("\nAvailable commands:\n");
    printf("  help              - Show this help message\n");
    printf("  list              - List online players\n");
    printf("  challenge <name>  - Challenge a player to a game\n");
    printf("  accept <name>     - Accept a challenge\n");
    printf("  refuse <name>     - Refuse a challenge\n");
    printf("  move <hole>       - Play a move (hole 0-5)\n");
    printf("  chat <message>    - Send a chat message\n");
    printf("  quit              - Disconnect and exit\n");
    printf("\n");
}
