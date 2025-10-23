# Awalé Game Server

Serveur de jeu Awalé multi-joueurs développé en C dans le cadre du TP 2/3/4 de Programmation Réseaux (4IF).

## 📋 Description

Ce projet implémente un serveur de jeu permettant à plusieurs clients de :
- Se connecter avec un nom d'utilisateur
- Défier d'autres joueurs
- Jouer des parties d'Awalé en respectant les règles officielles
- Communiquer via chat
- (À venir) Observer des parties en cours, gérer des tournois, etc.

## 🏗️ Architecture

Le projet est organisé en modules indépendants pour faciliter l'extensibilité :

```
Awal-Game-Server/
├── common/          # Code réseau commun (client/serveur)
│   ├── net.h/c      # Abstraction socket multiplateforme
│   └── protocol.h/c # Protocole de communication
├── game/            # Moteur de jeu Awalé (indépendant du réseau)
│   └── awale.h/c    # Logique du jeu, règles, affichage
├── server/          # Serveur de jeu
│   ├── server.c     # Boucle principale, gestion clients
│   └── session.c    # Gestion des parties en cours
└── client/          # Client de jeu
    └── client.c     # Interface utilisateur en ligne de commande
```

## 🎮 Règles du jeu Awalé

L'Awalé est un jeu traditionnel africain pour 2 joueurs. Chaque joueur possède 6 trous contenant initialement 4 graines. Le but est de capturer plus de 25 graines (sur 48 au total).

**Règles implémentées :**
- Distribution des graines dans le sens anti-horaire
- Capture des graines (2 ou 3 graines) sur le côté adverse
- Règle de famine : interdiction d'affamer l'adversaire
- Fin de partie quand un joueur a > 25 graines ou qu'aucun coup n'est possible

Voir : https://fr.wikipedia.org/wiki/Awalé

## 🔧 Compilation

### Prérequis
- GCC (ou compatible C99)
- Make
- Système Unix/Linux ou WSL sous Windows

### Commandes

```bash
# Compiler tout le projet
make

# Compiler uniquement le serveur
make awale_server

# Compiler uniquement le client
make awale_client

# Nettoyer et recompiler
make rebuild

# Nettoyer les fichiers objets
make clean
```

## 🚀 Utilisation

### Lancer le serveur

```bash
./awale_server
```

Le serveur écoute par défaut sur le port **1977**.

### Lancer un client

```bash
# Connexion locale
./awale_client

# Connexion à un serveur distant
./awale_client <adresse_IP> <port>
```

### Commandes client

Une fois connecté, vous pouvez utiliser :

```
help              - Affiche l'aide
list              - Liste les joueurs en ligne
challenge <nom>   - Défier un joueur
accept <nom>      - Accepter un défi
refuse <nom>      - Refuser un défi
move <trou>       - Jouer un coup (trou 0-5)
chat <message>    - Envoyer un message
quit              - Quitter
```

## 📊 État d'implémentation

### ✅ Étape 0 - Moteur de jeu (COMPLET)
- [x] Représentation du plateau
- [x] Validation des coups
- [x] Distribution des graines
- [x] Capture des graines
- [x] Détection de fin de partie
- [x] Affichage ASCII
- [x] Sauvegarde/chargement de partie

### 🚧 Étape 1-3 - Client/Serveur de base (EN COURS)
- [x] Architecture réseau multiplateforme
- [x] Protocole de messages
- [x] Connexion/déconnexion clients
- [x] Gestion sessions de jeu
- [ ] Liste des joueurs en ligne
- [ ] Système de défi/acceptation
- [ ] Diffusion état du jeu

### 📝 Étapes futures (À FAIRE)
- [ ] Chat général et privé
- [ ] Mode spectateur
- [ ] Biographie joueurs
- [ ] Historique des parties
- [ ] Système de classement ELO
- [ ] Tournois
- [ ] Reconnexion en cas de déconnexion

## 🧪 Tests

Pour tester le moteur de jeu indépendamment :

```bash
# Créer un fichier de test
cat > test_game.c << 'EOF'
#include "game/awale.h"

int main() {
    awale_game_t *game = awale_create();
    awale_print(game);
    
    awale_play_move(game, 2);
    awale_print(game);
    
    awale_free(game);
    return 0;
}
EOF

gcc -o test_game test_game.c game/awale.c
./test_game
```

## 📝 Format de protocole

Les messages client-serveur utilisent une structure simple :

```c
typedef struct {
    msg_type_t type;      // Type de message
    char sender[64];      // Expéditeur
    char recipient[64];   // Destinataire
    char data[1024];      // Données
} message_t;
```

Types de messages : LOGIN, CHALLENGE, PLAY_MOVE, GAME_STATE, CHAT, etc.

## 🤝 Contributeurs

- Louis LABORY (labory@insa-lyon.fr)

## 📄 Licence

Projet académique INSA Lyon - 4IF Programmation Réseaux

---

**Note :** Ce projet est en cours de développement. Les fonctionnalités marquées comme "À FAIRE" seront implémentées progressivement.