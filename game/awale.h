#ifndef AWALE_H
#define AWALE_H

/* Awale game constants */
#define HOLES_PER_PLAYER 6
#define TOTAL_HOLES 12
#define INITIAL_SEEDS 4
#define WINNING_SCORE 25

/* Game status codes */
typedef enum {
    AWALE_OK,
    AWALE_INVALID_MOVE,
    AWALE_INVALID_HOLE,
    AWALE_EMPTY_HOLE,
    AWALE_STARVATION_RULE,
    AWALE_GAME_OVER
} awale_status_t;

/* Game state structure */
typedef struct {
    int holes[TOTAL_HOLES];    /* holes[0-5] = player 0, holes[6-11] = player 1 */
    int scores[2];             /* scores for player 0 and player 1 */
    int current_player;        /* 0 or 1 */
    int game_over;             /* 1 if game is finished */
    int winner;                /* -1 = draw, 0 or 1 = winner */
} awale_game_t;

/* Game lifecycle */
awale_game_t* awale_create(void);
void awale_free(awale_game_t *game);
void awale_reset(awale_game_t *game);

/* Game operations */
void awale_switch_player(awale_game_t *game);
awale_status_t awale_play_move(awale_game_t *game, int hole);
int awale_is_valid_move(const awale_game_t *game, int hole);

/* Game state queries */
int awale_is_game_over(const awale_game_t *game);
int awale_get_winner(const awale_game_t *game);
int awale_get_score(const awale_game_t *game, int player);

/* Display and persistence */
void awale_print(const awale_game_t *game, const char *player0_name, const char *player1_name);
void awale_print_to_buffer(const awale_game_t *game, char *buffer, int size,
                                     const char *player0_name, const char *player1_name);
int awale_save(const awale_game_t *game, const char *filename);
awale_game_t* awale_load(const char *filename);

/* Helper functions */
const char* awale_status_string(awale_status_t status);

#endif /* AWALE_H */
