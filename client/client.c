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

    /* Connect to server */
    if (connect_to_server(server_host, server_port) < 0) {
        cleanup_client();
        return EXIT_FAILURE;
    }
    
    printf("Connected to server at %s:%d\n", server_host, server_port);
    
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
    
    /* Prompt for password and send login message (password sent in data field).
       Note: password is sent in clear over the local network in this simple protocol. */
    char password[128];
    printf("Enter password: ");
    fflush(stdout);
    if (fgets(password, sizeof(password), stdin) == NULL) password[0] = '\0';
    password[strcspn(password, "\n")] = '\0';

    /* Send login message */
    message_t login_msg;
    protocol_create_login(&login_msg, username, password);
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
            printf("\nType 'help' for available commands\n\n");
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
        char *opponent = input + 10;
        message_t msg;
        protocol_create_challenge(&msg, username, opponent);
        protocol_send_message(server_sock, &msg);
        printf("Challenge sent to %s\n", opponent);
    }
    else if (strncmp(input, "move ", 5) == 0) {
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
        message_t msg;
        char *rest = input + 5;
        while (*rest == ' ') rest++;
        char *space = strchr(rest, ' ');
        if (space != NULL) {
            /* Private chat */
            size_t recip_len = space - rest;
            char recip[64];
            if (recip_len >= sizeof(recip)) recip_len = sizeof(recip) - 1;
            strncpy(recip, rest, recip_len);
            recip[recip_len] = '\0';

            char *message_text = space + 1;
            protocol_create_chat(&msg, username, recip, message_text);
            protocol_send_message(server_sock, &msg);
        } else {
            /* Session chat */
            char *message_text = rest;
            protocol_create_chat(&msg, username, "", message_text);
            protocol_send_message(server_sock, &msg);
        }
    }
    else if (strcmp(input, "games") == 0) {
        /* Request game session list */
        message_t msg;
        protocol_create_message(&msg, MSG_LIST_GAMES, username, "", "");
        protocol_send_message(server_sock, &msg);
    }
    else if (strncmp(input, "spectate ", 8) == 0) {
        /* Request to observe a game session */
        int session_id = atoi(input + 8);
        message_t msg;
        char data[BUF_SIZE];
        snprintf(data, sizeof(data), "%d", session_id);
        protocol_create_message(&msg, MSG_SPECTATE, username, "", data);
        protocol_send_message(server_sock, &msg);
        printf("Requested to observe session %d\n", session_id);
    }
    else if (strcmp(input, "quit") == 0) {
        printf("Disconnecting...\n");
        exit(0);
    }
    else if (strncmp(input, "accept ", 7) == 0){
        message_t msg;
        protocol_create_message(&msg, MSG_CHALLENGE_ACCEPT, username, input +7, "");
        protocol_send_message(server_sock, &msg);
    }
    else if (strncmp(input, "refuse ", 7) == 0){
        message_t msg;
        protocol_create_message(&msg, MSG_CHALLENGE_REFUSE, username, input +7, "");
        protocol_send_message(server_sock, &msg);
    }
    else if(strncmp(input, "bio view ", 9) == 0){
        message_t msg;
        protocol_create_message(&msg, MSG_BIO_VIEW, username, input+9, "");
        protocol_send_message(server_sock, &msg);
    }
    else if(strcmp(input, "bio edit") == 0){
        printf("Write your bio now (up to 10 lines). Type '.done' on a line to finish early.\n");
        char lines[10][BUF_SIZE];
        int line_count = 0;
        for (int i = 0; i < 10; i++) {
            printf("%d> ", i+1);
            fflush(stdout);
            if (fgets(lines[i], sizeof(lines[i]), stdin) == NULL) {
                break;
            }
            lines[i][strcspn(lines[i], "\n")] = '\0';
            if (strcmp(lines[i], ".done") == 0) {
                break;
            }
            line_count++;
        }
        char bio[BUF_SIZE];
        bio[0] = '\0';
        int bio_len = 0;
        for (int i = 0; i < line_count; i++) {
            int n = snprintf(bio + bio_len, sizeof(bio) - bio_len, "%s%s", lines[i], (i + 1 < line_count) ? "\n" : "");
            if (n < 0 || n >= (int)(sizeof(bio) - bio_len)) {
                bio[sizeof(bio) - 1] = '\0';
                break;
            }
            bio_len += n;
        }

        message_t msg;
        protocol_create_message(&msg, MSG_BIO_EDIT, username, "", bio);
        protocol_send_message(server_sock, &msg);
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

        case MSG_CHALLENGE_REFUSE:
            printf("%s\n", msg.data);
            break;

        case MSG_GAME_LIST:
            printf("Active game sessions:\n%s\n", msg.data);
            break;

        case MSG_SPECTATE:
            printf("Now observing session\n");
            break;
            
        case MSG_BIO_VIEW:
            printf("Bio of %s:\n%s\n", msg.sender, msg.data);
            break;

        default:
            printf("Received unknown message type: %d\n", msg.type);
            break;
    }
}

static void print_help(void)
{
    printf("\nAvailable commands:\n");
    printf("  help                - Show this help message\n");
    printf("  list                - List online players\n");
    printf("  challenge <name>    - Challenge a player to a game\n");
    printf("  accept <name>       - Accept a challenge\n");
    printf("  refuse <name>       - Refuse a challenge\n");
    printf("  move <hole>         - Play a move (hole 0-5)\n");
    printf("  chat <msg>          - Send a session chat message\n");
    printf("  chat <player> <msg> - Send a private chat message\n");
    printf("  games               - List active game sessions\n");
    printf("  spectate <id>       - Observe a game session by id\n");
    printf("  bio view <pseudo>   - View the bio of a player\n");
    printf("  bio edit            - Edit your bio\n");
    printf("  quit                - Disconnect and exit\n");
    printf("\n");
}
