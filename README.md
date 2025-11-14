# Awalé Game Server

Serveur de jeu Awalé multi-joueurs développé en C dans le cadre du TP 2/3/4 de Programmation Réseaux (4IF).

## 📋 Description

Ce projet implémente un serveur de jeu permettant à plusieurs clients de :
- Se connecter avec un nom d'utilisateur
- Défier d'autres joueurs
- Jouer des parties d'Awalé en respectant les règles officielles
- Communiquer via chat
- et bien d'autres fonctionnalités...

## 🏗️ Architecture

- `server/` : code du serveur (`server.c`, `session.c`). Gère les connexions, sessions de jeu, stockage des comptes et persistance des parties.
- `client/` : client console (`client.c`) permettant de se connecter, défier, discuter et jouer.
- `common/` : bibliothèques partagées (`net.c`, `protocol.c`) gérant le transport bas-niveau et la structure des messages.
- `game/` : implémentation du moteur Awalé (`awale.c`, règles et état de partie).
- `saved_games/` : répertoire où les parties terminées sont enregistrées au format `.awale`.

Le serveur et le client communiquent via un protocole simple basé sur l'envoi d'une structure `message_t` (voir `common/protocol.h`).

## ⚙️ Prérequis

- Un environnement POSIX (Linux / WSL) ou Windows avec GCC compatible.
- `make` et `gcc` installés pour utiliser le `Makefile` fourni.

## 🔧 Compilation et exécution

Depuis la racine du projet, en WSL ou Linux :

```bash
# Compiler le serveur et le client
make

# Lancer le serveur (port par défaut : 1977)
./awale_server

# Lancer un client (optionnel : host port)
./awale_client 127.0.0.1 1977
```

Vous pouvez aussi utiliser les cibles `make run-server` et `make run-client` qui lancent respectivement le serveur et le client compilés.

Pour nettoyer les artefacts de build :

```bash
make clean
```

## 🗂 Fichiers importants

- `accounts.db` : fichier texte contenant les comptes (nom|hash|bio_escaped). Ne pas modifier à la main sans précautions.
- `saved_games/` : sauvegardes de parties terminées.
- `Makefile` : compilation et règles d'exécution.

## 🧭 Manuel utilisateur (commandes client)

Les commandes suivantes sont disponibles dans le client console (`client/client.c`). Tapez `help` en session pour afficher ces commandes.

- `help` : Affiche l'aide.
- `list` : Liste les joueurs actuellement en ligne.
- `challenge <name>` : Défier `<name>` ; le joueur ciblé reçoit une notification et peut accepter ou refuser.
- `accept <name>` : Accepte le défi provenant de `<name>`. Cette commande ne fonctionne que si `<name>` vous a effectivement challengé (le serveur garde une liste de demandes en attente).
- `refuse <name>` : Refuse le défi provenant de `<name>`.
- `move <hole>` : Jouer un coup sur le trou `0-5` (uniquement lorsque vous êtes en jeu).
- `chat <msg>` : Envoyer un message de session (à l'adversaire) si vous êtes en jeu.
- `chat <player> <msg>` : Envoyer un message privé à un autre joueur.
- `games` : Liste des sessions de jeu actives (identifiants et participants).
- `spectate <id>` : Demande à observer la session d'identifiant `<id>`.
- `bio view <pseudo>` : Voir la bio d'un joueur.
- `bio edit` : Éditer votre bio (multi‑ligne, terminez par `.done`).
- `give up` : Abandonner la partie en cours.
- `quit` : Déconnecter et quitter le client.

### Comportements notables
- Lorsqu'un mot de passe est invalide, le serveur renvoie `MSG_ERROR` (texte `Invalid password`) et ferme la connexion : le client détecte l'EOF et propose de retenter le mot de passe.
- Le serveur garde plusieurs demandes de défi en attente par joueur ; `accept <name>` ne fonctionne que si `<name>` figure dans votre liste de challengers en attente.

## 🎯 Bonnes pratiques et sécurité

- À l'heure actuelle, le mot de passe est envoyé en clair par le client et stocké / comparé de façon simplifiée. Il est fortement recommandé d'améliorer cela (hachage côté serveur avec sel unique, transport TLS ou méthode d'authentification sans mot de passe) pour une utilisation réseau réelle.
- Limitez l'accès au fichier `accounts.db` et considérez des protections contre le brute-force (verrouillage temporaire, temporisation).

## 🧪 Tests manuels rapides

1. Compiler (`make`).
2. Lancer le serveur : `./awale_server`.
3. Ouvrir deux terminaux et lancer `./awale_client` dans chacun.
4. Dans le client A : `challenge B`.
5. Dans le client B : vous verrez la notification et pouvez `accept A` ou `refuse A`.

## 🤖 Utilisation de l'IA
Nous avons utilisé le modèle GPT-5 mini d'OpenAI dans le cadre du développement de ce projet pour :
- nous aider dans la structuration/organisation du code,
- corriger notre code lorsqu'il était non fonctionnel,
- Générer le README.md et la documentation des fonctions.


