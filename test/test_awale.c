/**
 * test_awale.c - Interactive Awalé Game
 * 
 * This program allows you to play Awalé game in the terminal
 * to test the game logic independently from the network code.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../game/awale.h"

void print_help() {
    printf("\n=== Awalé Game - How to Play ===\n");
    printf("Rules:\n");
    printf("  - Each player has 6 holes numbered 0-5\n");
    printf("  - Player 0 controls holes 0-5 (bottom row)\n");
    printf("  - Player 1 controls holes 6-11 (top row)\n");
    printf("  - To play: enter the hole number (0-11)\n");
    printf("  - Seeds are distributed counter-clockwise\n");
    printf("  - Capture when landing on opponent's side with 2-3 seeds\n");
    printf("  - First to 25 seeds wins!\n");
    printf("\nCommands:\n");
    printf("  0-11  : Select hole to play\n");
    printf("  help  : Show this help\n");
    printf("  save  : Save game to file\n");
    printf("  load  : Load game from file\n");
    printf("  quit  : Exit game\n");
    printf("================================\n\n");
}

void play_game() {
    awale_game_t* game = awale_create();
    if (!game) {
        fprintf(stderr, "Failed to create game!\n");
        return;
    }

    awale_reset(game);
    printf("\n=== Welcome to Awalé! ===\n");
    print_help();

    char input[100];
    
    while (!awale_is_game_over(game)) {
        // Display current state
        awale_print(game, NULL, NULL);
        
        // Show whose turn it is
        printf("\n>>> Player %d's turn (", game->current_player);
        if (game->current_player == 0) {
            printf("holes 0-5");
        } else {
            printf("holes 6-11");
        }
        printf(")\n");
        printf("Enter your move: ");
        
        // Read input
        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }
        
        // Remove newline
        input[strcspn(input, "\n")] = '\0';
        
        // Parse command
        if (strcmp(input, "quit") == 0) {
            printf("Thanks for playing!\n");
            break;
        }
        else if (strcmp(input, "help") == 0) {
            print_help();
            continue;
        }
        else if (strcmp(input, "save") == 0) {
            printf("Enter filename: ");
            if (fgets(input, sizeof(input), stdin)) {
                input[strcspn(input, "\n")] = '\0';
                if (awale_save(game, input) == 0) {
                    printf("Game saved to '%s'\n", input);
                } else {
                    printf("Failed to save game!\n");
                }
            }
            continue;
        }
        else if (strcmp(input, "load") == 0) {
            printf("Enter filename: ");
            if (fgets(input, sizeof(input), stdin)) {
                input[strcspn(input, "\n")] = '\0';
                awale_game_t* loaded_game = awale_load(input);
                if (loaded_game) {
                    awale_free(game);
                    game = loaded_game;
                    printf("Game loaded from '%s'\n", input);
                } else {
                    printf("Failed to load game!\n");
                }
            }
            continue;
        }
        
        // Try to parse as hole number
        char* endptr;
        long hole = strtol(input, &endptr, 10);
        
        // Check if conversion was successful
        if (*endptr != '\0' || endptr == input) {
            printf("Invalid input! Enter a hole number (0-11), 'help', 'save', 'load', or 'quit'\n");
            continue;
        }
        
        // Validate and play move
        if (hole < 0 || hole >= TOTAL_HOLES) {
            printf("Invalid hole! Choose between 0 and 11\n");
            continue;
        }
        
        awale_status_t status = awale_play_move(game, (int)hole);
        
        if (status != AWALE_OK) {
            printf("Invalid move: %s\n", awale_status_string(status));
            printf("Try again!\n");
        }
    }
    
    // Game over - show results
    if (awale_is_game_over(game)) {
        printf("\n");
        awale_print(game, NULL, NULL);
        printf("\n=== GAME OVER ===\n");
        printf("Player 0 score: %d\n", awale_get_score(game, 0));
        printf("Player 1 score: %d\n", awale_get_score(game, 1));
        
        int winner = awale_get_winner(game);
        if (winner == -1) {
            printf("It's a draw!\n");
        } else {
            printf("Player %d wins!\n", winner);
        }
        printf("=================\n\n");
    }
    
    awale_free(game);
}

int main() {
    while (1) {
        play_game();
        
        printf("Play again? (y/n): ");
        char input[10];
        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }
        
        if (input[0] != 'y' && input[0] != 'Y') {
            printf("Goodbye!\n");
            break;
        }
    }
    
    return 0;
}
