#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

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

void net_cleanup(void)
{
#ifdef WIN32
    WSACleanup();
#endif
}

SOCKET net_create_socket(void)
{
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        perror("socket");
        return INVALID_SOCKET;
    }
    
    /* Set socket options for reusability */
#ifndef WIN32
    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
    }
#endif
    
    return sock;
}

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

int net_listen_socket(SOCKET sock, int backlog)
{
    if (listen(sock, backlog) == SOCKET_ERROR) {
        perror("listen");
        return -1;
    }
    return 0;
}

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

int net_send(SOCKET sock, const char *buffer, int len)
{
    int sent = send(sock, buffer, len, 0);
    if (sent < 0) {
        perror("send");
        return -1;
    }
    return sent;
}

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

void net_close(SOCKET sock)
{
    closesocket(sock);
}
