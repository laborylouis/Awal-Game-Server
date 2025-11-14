#include "awale.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// Create and initialize a new Awalé game object.
awale_game_t* awale_create(void){

    awale_game_t *game = (awale_game_t*)malloc(sizeof(awale_game_t));
    if (!game){
        return NULL;
    }
    awale_reset(game);
    return game;
}

// Reset an existing game state to the initial configuration.
void awale_reset(awale_game_t *game){
    for (int i = 0; i< 2; ++i) game->scores[i]=0;
    for (int i = 0; i < TOTAL_HOLES; ++i) game->holes[i] = 4;  
    game->game_over = 0;
    game->current_player = 0;
    game->winner = -1;
}

// Free a previously allocated game object.
void awale_free(awale_game_t *game){
    if (game) {
        free(game);
    }
}

// Switch the current player (toggle between player 0 and 1).
void awale_switch_player(awale_game_t *game){
    if (game){
        game->current_player = 1 - game->current_player;
    }
}

// Check whether a move (hole index) is valid for the current player.
int awale_is_valid_move(const awale_game_t *game, int hole){
    if (!game) {
        return 0;
    }
    
    if (game->game_over) {
        return 0;
    }
    
    if (hole < 0 || hole >= TOTAL_HOLES) {
        return 0;
    }
    
    int player = game->current_player;
    int start = player * HOLES_PER_PLAYER;
    int end = start + HOLES_PER_PLAYER;
    
    if (hole < start || hole >= end) {
        return 0;
    }
    
    if (game->holes[hole] == 0) {
        return 0;
    }
    
    return 1;
}

// Execute a move: sow seeds from `hole`, apply capture rules and update scores.
// Returns an awale_status_t indicating success or error reason.
awale_status_t awale_play_move(awale_game_t *game, int hole){
    if (!awale_is_valid_move(game, hole)) {
        if (!game) return AWALE_INVALID_MOVE;
        if (game->game_over) return AWALE_GAME_OVER;
        if (hole < 0 || hole >= TOTAL_HOLES) return AWALE_INVALID_HOLE;
        if (game->holes[hole] == 0) return AWALE_EMPTY_HOLE;
        return AWALE_INVALID_MOVE;
    }
    
    int player = game->current_player;
    int seeds = game->holes[hole];
    game->holes[hole] = 0;
    int current = hole;
    
    while (seeds > 0) {
        current = (current + 1) % TOTAL_HOLES;
        
        if (current != hole) {
            game->holes[current]++;
            seeds--;
        }
    }
    
    int opponent = 1 - player;
    int opp_start = opponent * HOLES_PER_PLAYER;
    int opp_end = opp_start + HOLES_PER_PLAYER;
    
    while (current >= opp_start && current < opp_end &&
           (game->holes[current] == 2 || game->holes[current] == 3)) {
        
        game->scores[player] += game->holes[current];
        game->holes[current] = 0;
        
        current = (current - 1 + TOTAL_HOLES) % TOTAL_HOLES;
    }
    
    awale_switch_player(game);
    
    if (game->scores[0] > WINNING_SCORE || game->scores[1] > WINNING_SCORE) {
        game->game_over = 1;
        game->winner = (game->scores[0] > game->scores[1]) ? 0 : 1;
        return AWALE_OK;
    }
    
    int player0_has_seeds = 0;
    int player1_has_seeds = 0;
    
    for (int i = 0; i < HOLES_PER_PLAYER; i++) {
        if (game->holes[i] > 0) player0_has_seeds = 1;
        if (game->holes[i + HOLES_PER_PLAYER] > 0) player1_has_seeds = 1;
    }
    
    if (!player0_has_seeds || !player1_has_seeds) {
        for (int i = 0; i < HOLES_PER_PLAYER; i++) {
            game->scores[0] += game->holes[i];
            game->holes[i] = 0;
        }
        for (int i = HOLES_PER_PLAYER; i < TOTAL_HOLES; i++) {
            game->scores[1] += game->holes[i];
            game->holes[i] = 0;
        }
        
        game->game_over = 1;
        
        if (game->scores[0] > game->scores[1]) {
            game->winner = 0;
        } else if (game->scores[1] > game->scores[0]) {
            game->winner = 1;
        } else {
            game->winner = -1;
        }
    }
    
    return AWALE_OK;
}

// Return whether the game has finished.
int awale_is_game_over(const awale_game_t *game){
    if (!game) {
        return 1;
    }
    return game->game_over;
}

// Get the winner index (0 or 1) or -1 if draw/unknown.
int awale_get_winner(const awale_game_t *game){
    if (!game) {
        return -1;
    }
    return game->winner;
}

// Retrieve the score for the specified player (0 or 1).
int awale_get_score(const awale_game_t *game, int player){
    if (!game || player < 0 || player > 1) {
        return -1;
    }
    return game->scores[player];
}

// Pretty-print the current board and scores to stdout with optional player names.
void awale_print(const awale_game_t *game, const char *player0_name, const char *player1_name){
    if (!game) return;

        const char *p0 = player0_name ? player0_name : "Player 0";
        const char *p1 = player1_name ? player1_name : "Player 1";
        char top_label[128];
        char bottom_label[128];
        snprintf(top_label, sizeof(top_label), "%s:  ", p1);
        snprintf(bottom_label, sizeof(bottom_label), "%s:  ", p0);
        size_t max_label = strlen(top_label) > strlen(bottom_label) ? strlen(top_label) : strlen(bottom_label);

        printf("%*s", (int)max_label, "");
        for (int i = TOTAL_HOLES - 1; i >= HOLES_PER_PLAYER; i--) {
            printf(" %2d  ", i);
        }
        printf("\n");

        printf("%-*s", (int)max_label, top_label);
        for (int i = TOTAL_HOLES - 1; i >= HOLES_PER_PLAYER; i--) {
            printf("[%2d] ", game->holes[i]);
        }
        printf("  Score: %d\n", game->scores[1]);

        printf("%-*s", (int)max_label, bottom_label);
        for (int i = 0; i < HOLES_PER_PLAYER; i++) {
            printf("[%2d] ", game->holes[i]);
        }
        printf("  Score: %d\n", game->scores[0]);

        printf("%*s", (int)max_label, "");
        for (int i = 0; i < HOLES_PER_PLAYER; i++) {
            printf(" %2d  ", i);
        }
        printf("\n");

    if (game->game_over) {
        printf("\nGAME OVER! ");
        if (game->winner == -1) {
            printf("Draw!\n");
        } else {
            const char *wname = (game->winner == 0) ? (player0_name ? player0_name : "Player 0") : (player1_name ? player1_name : "Player 1");
            printf("Winner: %s\n", wname);
        }
    } else {
        const char *cur = (game->current_player == 0) ? (player0_name ? player0_name : "Player 0") : (player1_name ? player1_name : "Player 1");
        printf("\nCurrent player: %s\n", cur);
    }
    printf("\n");
}



// Format the board and status into `buffer` (safe snprintf usage).
void awale_print_to_buffer(const awale_game_t *game, char *buffer, int size,
                                     const char *player0_name, const char *player1_name){
    if (!game || !buffer || size <= 0) return;

    int offset = 0;

    offset += snprintf(buffer + offset, size - offset, "\n");
    const char *p0 = player0_name ? player0_name : "Player 0";
    const char *p1 = player1_name ? player1_name : "Player 1";
    char top_label[128];
    char bottom_label[128];
    snprintf(top_label, sizeof(top_label), "%s:  ", p1);
    snprintf(bottom_label, sizeof(bottom_label), "%s:  ", p0);
    size_t max_label = strlen(top_label) > strlen(bottom_label) ? strlen(top_label) : strlen(bottom_label);

    offset += snprintf(buffer + offset, size - offset, "\n");
    offset += snprintf(buffer + offset, size - offset, "%*s", (int)max_label, "");
    for (int i = TOTAL_HOLES - 1; i >= HOLES_PER_PLAYER; i--) {
        offset += snprintf(buffer + offset, size - offset, " %2d  ", i);
    }
    offset += snprintf(buffer + offset, size - offset, "\n");

    offset += snprintf(buffer + offset, size - offset, "%-*s", (int)max_label, top_label);
    for (int i = TOTAL_HOLES - 1; i >= HOLES_PER_PLAYER; i--) {
        offset += snprintf(buffer + offset, size - offset, "[%2d] ", game->holes[i]);
    }
    offset += snprintf(buffer + offset, size - offset, "  Score: %d\n", game->scores[1]);

    offset += snprintf(buffer + offset, size - offset, "%-*s", (int)max_label, bottom_label);
    for (int i = 0; i < HOLES_PER_PLAYER; i++) {
        offset += snprintf(buffer + offset, size - offset, "[%2d] ", game->holes[i]);
    }
    offset += snprintf(buffer + offset, size - offset, "  Score: %d\n", game->scores[0]);

    offset += snprintf(buffer + offset, size - offset, "%*s", (int)max_label, "");
    for (int i = 0; i < HOLES_PER_PLAYER; i++) {
        offset += snprintf(buffer + offset, size - offset, " %2d  ", i);
    }
    offset += snprintf(buffer + offset, size - offset, "\n");

    if (game->game_over) {
        offset += snprintf(buffer + offset, size - offset, "\nGAME OVER! ");
        if (game->winner == -1) {
            offset += snprintf(buffer + offset, size - offset, "Draw!\n");
        } else {
            const char *wname = (game->winner == 0) ? (player0_name ? player0_name : "Player 0") : (player1_name ? player1_name : "Player 1");
            offset += snprintf(buffer + offset, size - offset, "Winner: %s\n", wname);
        }
    } else {
        const char *cur = (game->current_player == 0) ? (player0_name ? player0_name : "Player 0") : (player1_name ? player1_name : "Player 1");
        offset += snprintf(buffer + offset, size - offset, "\nCurrent player: %s\n", cur);
    }
}

// Save a simple textual snapshot of the game to `filename`.
int awale_save(const awale_game_t *game, const char *filename){
    if (!game || !filename) {
        return -1;
    }
    
    FILE *f = fopen(filename, "w");
    if (!f) {
        perror("fopen");
        return -1;
    }
    
    fprintf(f, "%d %d %d %d\n", game->current_player, game->game_over, 
            game->winner, game->scores[0]);
    fprintf(f, "%d\n", game->scores[1]);
    
    for (int i = 0; i < TOTAL_HOLES; i++) {
        fprintf(f, "%d ", game->holes[i]);
    }
    fprintf(f, "\n");
    
    fclose(f);
    return 0;
}

// Load a game snapshot from `filename`, returning a newly allocated game object.
awale_game_t* awale_load(const char *filename){
    if (!filename) {
        return NULL;
    }
    
    FILE *f = fopen(filename, "r");
    if (!f) {
        perror("fopen");
        return NULL;
    }
    
    awale_game_t *game = awale_create();
    if (!game) {
        fclose(f);
        return NULL;
    }
    
    if (fscanf(f, "%d %d %d %d", &game->current_player, &game->game_over,
               &game->winner, &game->scores[0]) != 4) {
        fclose(f);
        awale_free(game);
        return NULL;
    }
    
    if (fscanf(f, "%d", &game->scores[1]) != 1) {
        fclose(f);
        awale_free(game);
        return NULL;
    }
    
    for (int i = 0; i < TOTAL_HOLES; i++) {
        if (fscanf(f, "%d", &game->holes[i]) != 1) {
            fclose(f);
            awale_free(game);
            return NULL;
        }
    }
    
    fclose(f);
    return game;
}

// Convert an awale_status_t value to a human-readable string.
const char* awale_status_string(awale_status_t status){
    switch (status) {
        case AWALE_OK:
            return "OK";
        case AWALE_INVALID_MOVE:
            return "Invalid move";
        case AWALE_INVALID_HOLE:
            return "Invalid hole";
        case AWALE_EMPTY_HOLE:
            return "Empty hole";
        case AWALE_STARVATION_RULE:
            return "Starvation rule violation";
        case AWALE_GAME_OVER:
            return "Game over";
        default:
            return "Unknown status";
    }
}    
