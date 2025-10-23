#ifndef NET_H
#define NET_H

/* Platform-specific network includes and type definitions */

#ifdef WIN32

#include <winsock2.h>

#elif defined(__linux__) || defined(__unix__) || defined(__APPLE__)

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>

#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket(s) close(s)

typedef int SOCKET;
typedef struct sockaddr_in SOCKADDR_IN;
typedef struct sockaddr SOCKADDR;
typedef struct in_addr IN_ADDR;

#else
#error Platform not supported
#endif

/* Common network constants */
#define DEFAULT_PORT 1977
#define MAX_CLIENTS 100
#define BUF_SIZE 1024

/* Network initialization and cleanup */
void net_init(void);
void net_cleanup(void);

/* Socket operations */
SOCKET net_create_socket(void);
int net_bind_socket(SOCKET sock, int port);
int net_listen_socket(SOCKET sock, int backlog);
SOCKET net_accept_connection(SOCKET sock, SOCKADDR_IN *client_addr);
int net_connect(SOCKET sock, const char *host, int port);

/* Data transfer */
int net_send(SOCKET sock, const char *buffer, int len);
int net_recv(SOCKET sock, char *buffer, int max_len);
void net_close(SOCKET sock);

#endif /* NET_H */
