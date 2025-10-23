#include <stdio.h>
#include "game/awale.h"

int main(void)
{
    printf("=== Test du moteur Awale ===\n\n");
    
    /* Créer une nouvelle partie */
    awale_game_t *game = awale_create();
    if (!game) {
        fprintf(stderr, "Erreur: impossible de créer la partie\n");
        return 1;
    }
    
    printf("Partie initiale:\n");
    awale_print(game);
    
    printf("\n--- Test de quelques coups ---\n");
    
    /* Joueur 0 joue le trou 2 */
    printf("\nJoueur 0 joue le trou 2:\n");
    awale_status_t status = awale_play_move(game, 2);
    printf("Status: %s\n", awale_status_string(status));
    awale_print(game);
    
    /* Joueur 1 joue le trou 8 (= trou 2 de joueur 1) */
    printf("\nJoueur 1 joue le trou 8:\n");
    status = awale_play_move(game, 8);
    printf("Status: %s\n", awale_status_string(status));
    awale_print(game);
    
    /* Joueur 0 joue le trou 0 */
    printf("\nJoueur 0 joue le trou 0:\n");
    status = awale_play_move(game, 0);
    printf("Status: %s\n", awale_status_string(status));
    awale_print(game);
    
    /* Test d'un coup invalide */
    printf("\n--- Test coup invalide ---\n");
    printf("Joueur 1 essaie de jouer un trou vide (trou 0):\n");
    status = awale_play_move(game, 0);
    printf("Status: %s\n", awale_status_string(status));
    
    printf("\n--- Test sauvegarde/chargement ---\n");
    const char *filename = "test_save.awl";
    if (awale_save(game, filename) == 0) {
        printf("Partie sauvegardée dans %s\n", filename);
        
        awale_game_t *loaded = awale_load(filename);
        if (loaded) {
            printf("Partie chargée depuis %s:\n", filename);
            awale_print(loaded);
            awale_free(loaded);
        } else {
            printf("Erreur lors du chargement\n");
        }
    } else {
        printf("Erreur lors de la sauvegarde\n");
    }
    
    /* Libérer la mémoire */
    awale_free(game);
    
    printf("\n=== Test terminé avec succès ===\n");
    return 0;
}
