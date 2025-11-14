#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// Lightweight network helpers used by both server and client.
// These wrap common socket operations to keep higher-level code cleaner.

// Initialize network stack on Windows. No-op on POSIX.

void net_init(void)
{
#ifdef WIN32
    WSADATA wsa;
    int err = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (err < 0) {
        fprintf(stderr, "WSAStartup failed!\n");
        exit(EXIT_FAILURE);
    }
#endif
}

// Clean up network stack (Windows). No-op on POSIX.

void net_cleanup(void)
{
#ifdef WIN32
    WSACleanup();
#endif
}

// Create a TCP socket. On POSIX, set SO_REUSEADDR to make restart easier during development.
// Returns INVALID_SOCKET on error.

SOCKET net_create_socket(void)
{
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        perror("socket");
        return INVALID_SOCKET;
    }
    
#ifndef WIN32
    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
    }
#endif
    
    return sock;
}

// Bind a socket to all network interfaces on the given port.
// Returns 0 on success, -1 on failure.

int net_bind_socket(SOCKET sock, int port)
{
    SOCKADDR_IN addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    
    if (bind(sock, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        perror("bind");
        return -1;
    }
    return 0;
}

// Put a socket into listening state with the provided backlog.

int net_listen_socket(SOCKET sock, int backlog)
{
    if (listen(sock, backlog) == SOCKET_ERROR) {
        perror("listen");
        return -1;
    }
    return 0;
}

// Accept a new incoming connection. Returns the client socket or INVALID_SOCKET on error.
// client_addr is filled with the remote address (if non-NULL).

SOCKET net_accept_connection(SOCKET sock, SOCKADDR_IN *client_addr)
{
    socklen_t addr_len = sizeof(*client_addr);
    SOCKET client_sock = accept(sock, (SOCKADDR*)client_addr, &addr_len);
    
    if (client_sock == INVALID_SOCKET) {
        perror("accept");
        return INVALID_SOCKET;
    }
    return client_sock;
}

// Connect a socket to a remote IPv4 address and port. Returns 0 on success.

int net_connect(SOCKET sock, const char *host, int port)
{
    SOCKADDR_IN addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address: %s\n", host);
        return -1;
    }
    
    if (connect(sock, (SOCKADDR*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        return -1;
    }
    return 0;
}

// Wrap send(). Returns number of bytes sent or -1 on error.


int net_send(SOCKET sock, const char *buffer, int len)
{
    int sent = send(sock, buffer, len, 0);
    if (sent < 0) {
        perror("send");
        return -1;
    }
    return sent;
}

// Simple recv wrapper that null-terminates the received data (useful for text messages).
// Returns number of bytes received, 0 on orderly shutdown, or -1 on error.

int net_recv(SOCKET sock, char *buffer, int max_len)
{
    int received = recv(sock, buffer, max_len - 1, 0);
    if (received < 0) {
        perror("recv");
        return -1;
    }
    buffer[received] = '\0';
    return received;
}

// Close a socket (closesocket is aliased to close on POSIX builds).

void net_close(SOCKET sock)
{
    closesocket(sock);
}

