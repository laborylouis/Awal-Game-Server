# Awalé Game Server

Awalé multiplayer game server written in C as part of the Network Programming lab work (TP 2/3/4, 4IF).

## 📋 Description

This project implements a game server that allows multiple clients to:
- Connect with a username
- Challenge other players
- Play Awalé matches following the official rules
- Communicate via chat
- And other features in progress

## 🏗️ Architecture

- `server/`: server code (`server.c`, `session.c`) — handles connections, game sessions, account storage and game persistence.
- `client/`: console client (`client.c`) — connect, challenge, chat and play.
- `common/`: shared libraries (`net.c`, `protocol.c`) that provide low-level transport and message structures.
- `game/`: Awalé engine implementation (`awale.c`) and game state.
- `saved_games/`: directory where finished games are saved as `.awale` files.

The server and client communicate using a simple protocol that sends a `message_t` structure (see `common/protocol.h`).

## 🗂 Important files

- `accounts.db`: text file holding accounts in the format `name|hash|bio_escaped`. Do not edit manually without care.
- `saved_games/`: saved finished games.
- `Makefile`: build and run rules.

## ⚙️ Prerequisites

- A POSIX environment (Linux / WSL) or Windows with a compatible GCC toolchain.
- `make` and `gcc` installed to use the provided `Makefile`.

## 🔧 Build & Run

From the project root (WSL or Linux):

```bash
# Build server and client
make

# Run the server (default port: 1977)
./awale_server

# Run a client (optional: host port)
./awale_client 127.0.0.1 1977
```

You can also use the `make run-server` and `make run-client` targets to run the compiled server and client.

To clean build artifacts:

```bash
make clean
```

## 🧭 User manual (client commands)

The following commands are available in the console client (`client/client.c`). Type `help` during a session to display them.

- `help`: Show the help text.
- `list`: Show currently online players.
- `challenge <name>`: Challenge `<name>`; the target player receives a prompt and may accept or refuse.
- `accept <name>`: Accept a challenge from `<name>`. This only works if `<name>` actually challenged you (the server keeps a list of pending challenge requests).
- `refuse <name>`: Refuse a challenge from `<name>`.
- `move <hole>`: Play a move on hole `0-5` (only while in a game).
- `chat <msg>`: Send a session chat message to your opponent (when in a game).
- `chat <player> <msg>`: Send a private chat message to another player.
- `games`: List active game sessions (IDs and participants).
- `spectate <id>`: Request to observe session with id `<id>`.
- `bio view <pseudo>`: View a player's bio.
- `bio edit`: Edit your bio (multi-line; finish with `.done`).
- `give up`: Give up the current game.
- `quit`: Disconnect and exit the client.

## 🤖 AI usage
We used an AI assistant during development (GPT-5 mini) to help with code structuring for the beginning of the project, bug fixes and documentation generation.



