#include "awale.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ==================== Game Lifecycle ==================== */

awale_game_t* awale_create(void)
{
    awale_game_t *game = (awale_game_t*)malloc(sizeof(awale_game_t));
    if (!game) return NULL;
    
    awale_reset(game);
    return game;
}

void awale_free(awale_game_t *game)
{
    if (game) {
        free(game);
    }
}

void awale_reset(awale_game_t *game)
{
    if (!game) return;
    
    /* Initialize each hole with 4 seeds */
    for (int i = 0; i < TOTAL_HOLES; i++) {
        game->holes[i] = INITIAL_SEEDS;
    }
    
    /* Reset scores */
    game->scores[0] = 0;
    game->scores[1] = 0;
    
    /* Player 0 starts */
    game->current_player = 0;
    game->game_over = 0;
    game->winner = -1;
}

/* ==================== Helper Functions ==================== */

static int get_opponent(int player)
{
    return 1 - player;
}

static int is_player_hole(int hole, int player)
{
    if (player == 0) {
        return hole >= 0 && hole < HOLES_PER_PLAYER;
    } else {
        return hole >= HOLES_PER_PLAYER && hole < TOTAL_HOLES;
    }
}

static int player_has_seeds(const awale_game_t *game, int player)
{
    int start = player * HOLES_PER_PLAYER;
    int end = start + HOLES_PER_PLAYER;
    
    for (int i = start; i < end; i++) {
        if (game->holes[i] > 0) {
            return 1;
        }
    }
    return 0;
}

static void collect_remaining_seeds(awale_game_t *game)
{
    /* At end of game, each player collects seeds on their side */
    for (int i = 0; i < HOLES_PER_PLAYER; i++) {
        game->scores[0] += game->holes[i];
        game->holes[i] = 0;
    }
    
    for (int i = HOLES_PER_PLAYER; i < TOTAL_HOLES; i++) {
        game->scores[1] += game->holes[i];
        game->holes[i] = 0;
    }
}

static void check_game_over(awale_game_t *game)
{
    /* Game over if one player has more than half the seeds */
    if (game->scores[0] > WINNING_SCORE || game->scores[1] > WINNING_SCORE) {
        game->game_over = 1;
        game->winner = game->scores[0] > game->scores[1] ? 0 : 1;
        return;
    }
    
    /* Game over if one side is completely empty and can't be fed */
    if (!player_has_seeds(game, 0) || !player_has_seeds(game, 1)) {
        collect_remaining_seeds(game);
        game->game_over = 1;
        
        if (game->scores[0] > game->scores[1]) {
            game->winner = 0;
        } else if (game->scores[1] > game->scores[0]) {
            game->winner = 1;
        } else {
            game->winner = -1; /* Draw */
        }
    }
}

/* ==================== Game Validation ==================== */

int awale_is_valid_move(const awale_game_t *game, int hole)
{
    if (!game || game->game_over) {
        return 0;
    }
    
    /* Hole must be in current player's range */
    if (!is_player_hole(hole, game->current_player)) {
        return 0;
    }
    
    /* Hole must not be empty */
    if (game->holes[hole] == 0) {
        return 0;
    }
    
    return 1;
}

/* ==================== Game Logic ==================== */

awale_status_t awale_play_move(awale_game_t *game, int hole)
{
    if (!game) {
        return AWALE_INVALID_MOVE;
    }
    
    if (game->game_over) {
        return AWALE_GAME_OVER;
    }
    
    /* Validate hole index */
    if (hole < 0 || hole >= TOTAL_HOLES) {
        return AWALE_INVALID_HOLE;
    }
    
    /* Check if it's the player's hole */
    if (!is_player_hole(hole, game->current_player)) {
        return AWALE_INVALID_MOVE;
    }
    
    /* Check if hole is not empty */
    if (game->holes[hole] == 0) {
        return AWALE_EMPTY_HOLE;
    }
    
    /* Distribute seeds */
    int seeds = game->holes[hole];
    game->holes[hole] = 0;
    int current = hole;
    
    while (seeds > 0) {
        current = (current + 1) % TOTAL_HOLES;
        
        /* Don't put seed in the starting hole if we lap */
        if (current != hole) {
            game->holes[current]++;
            seeds--;
        }
    }
    
    /* Capture logic: if last seed lands on opponent's side with 2 or 3 seeds */
    int opponent = get_opponent(game->current_player);
    
    while (is_player_hole(current, opponent) && 
           (game->holes[current] == 2 || game->holes[current] == 3)) {
        
        game->scores[game->current_player] += game->holes[current];
        game->holes[current] = 0;
        
        /* Continue capturing backwards */
        current = (current - 1 + TOTAL_HOLES) % TOTAL_HOLES;
    }
    
    /* Check starvation rule: opponent must have at least one seed */
    if (!player_has_seeds(game, opponent)) {
        /* TODO: Implement starvation rule properly
         * If the move leaves opponent with no seeds, it might be invalid
         * depending on whether the player could have fed the opponent
         */
    }
    
    /* Switch to next player */
    awale_switch_player(game);
    
    /* Check if game is over */
    check_game_over(game);
    
    return AWALE_OK;
}

void awale_switch_player(awale_game_t *game)
{
    if (game) {
        game->current_player = get_opponent(game->current_player);
    }
}

/* ==================== Game State Queries ==================== */

int awale_is_game_over(const awale_game_t *game)
{
    return game ? game->game_over : 1;
}

int awale_get_winner(const awale_game_t *game)
{
    return game ? game->winner : -1;
}

int awale_get_score(const awale_game_t *game, int player)
{
    if (!game || player < 0 || player > 1) {
        return -1;
    }
    return game->scores[player];
}

/* ==================== Display ==================== */

void awale_print(const awale_game_t *game)
{
    if (!game) return;
    
    printf("\n");
    printf("Player 1:  ");
    for (int i = TOTAL_HOLES - 1; i >= HOLES_PER_PLAYER; i--) {
        printf("[%2d] ", game->holes[i]);
    }
    printf("  Score: %d\n", game->scores[1]);
    
    printf("Player 0:  ");
    for (int i = 0; i < HOLES_PER_PLAYER; i++) {
        printf("[%2d] ", game->holes[i]);
    }
    printf("  Score: %d\n", game->scores[0]);
    
    printf("           ");
    for (int i = 0; i < HOLES_PER_PLAYER; i++) {
        printf(" %2d  ", i);
    }
    printf("\n");
    
    if (game->game_over) {
        printf("\nGAME OVER! ");
        if (game->winner == -1) {
            printf("Draw!\n");
        } else {
            printf("Winner: Player %d\n", game->winner);
        }
    } else {
        printf("\nCurrent player: %d\n", game->current_player);
    }
    printf("\n");
}

void awale_print_to_buffer(const awale_game_t *game, char *buffer, int size)
{
    if (!game || !buffer || size <= 0) return;
    
    int offset = 0;
    
    offset += snprintf(buffer + offset, size - offset, "\n");
    offset += snprintf(buffer + offset, size - offset, "Player 1:  ");
    for (int i = TOTAL_HOLES - 1; i >= HOLES_PER_PLAYER; i--) {
        offset += snprintf(buffer + offset, size - offset, "[%2d] ", game->holes[i]);
    }
    offset += snprintf(buffer + offset, size - offset, "  Score: %d\n", game->scores[1]);
    
    offset += snprintf(buffer + offset, size - offset, "Player 0:  ");
    for (int i = 0; i < HOLES_PER_PLAYER; i++) {
        offset += snprintf(buffer + offset, size - offset, "[%2d] ", game->holes[i]);
    }
    offset += snprintf(buffer + offset, size - offset, "  Score: %d\n", game->scores[0]);
    
    if (game->game_over) {
        offset += snprintf(buffer + offset, size - offset, "\nGAME OVER! ");
        if (game->winner == -1) {
            offset += snprintf(buffer + offset, size - offset, "Draw!\n");
        } else {
            offset += snprintf(buffer + offset, size - offset, "Winner: Player %d\n", game->winner);
        }
    } else {
        offset += snprintf(buffer + offset, size - offset, "\nCurrent player: %d\n", game->current_player);
    }
}

/* ==================== Persistence ==================== */

int awale_save(const awale_game_t *game, const char *filename)
{
    if (!game || !filename) return -1;
    
    FILE *f = fopen(filename, "w");
    if (!f) {
        perror("fopen");
        return -1;
    }
    
    /* Save game state in simple text format */
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

awale_game_t* awale_load(const char *filename)
{
    if (!filename) return NULL;
    
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

/* ==================== Utility ==================== */

const char* awale_status_string(awale_status_t status)
{
    switch (status) {
        case AWALE_OK: return "OK";
        case AWALE_INVALID_MOVE: return "Invalid move";
        case AWALE_INVALID_HOLE: return "Invalid hole";
        case AWALE_EMPTY_HOLE: return "Empty hole";
        case AWALE_STARVATION_RULE: return "Starvation rule violation";
        case AWALE_GAME_OVER: return "Game over";
        default: return "Unknown status";
    }
}
