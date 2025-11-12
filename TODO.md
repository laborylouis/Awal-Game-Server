# Features
- points 8 et 9 (mode private, liste d'amis, et l'enregistrement des games)
- point 10 (ajouter ce qu'on veut): Par exemple :
    - Ajouter un elo (et faire en sorte de pouvoir le voir sur le profil des autres joueurs) NON
    - Ajouter un système de chat global PEUT ETRE
    - Ajouter un spinner pendant l'attente du joueur adverse NON
    - Ajouter un système de statistiques (nombre de parties jouées, gagnées, perdues, etc) OUI
    - Ajouter un système de niveaux/rangs (bronze argent or platine diamant maitre grand maitre) NON
    - Ajouter un système de matchmaking NON
    - Ajouter un système de tournois ?? NON
    - Ajouter un système de clans/guildes NON

- soigner l'interface (si on est très très chauds on peut faire une interface qui s'update plutot que des commandes les unes sous les autres)
- Ajouter un message quand un joueur quitte la partie (comme quand le serveur crash)
- Ajouter des docblocks pour toutes les fonctions
- Ajouter des tests unitaires pour les fonctions critiques (gestion des comptes, logique de jeu)
- changer le nom des commandes addfriend et removefriend en friend add et friend remove (avec un espace)
- change password
- mettre à jour le readme

# Bugs
- quand un joueur ferme son client l'autre est toujours marqué in game (peut être cleanup_client à modifier ?)
- faire en sorte qu'il n'y ait pas de deprecated quand on compile
- éviter de fermer le programme si le user entre un mauvais mot de passe (juste redemander le mot de passe) => ajouter un endpoint au protocole
- ne pas autoriser les noms de joueurs avec des espaces (côté client et serveur)