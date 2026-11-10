#ifndef NET_H
#define NET_H

#include <stdint.h>
#include <stddef.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#define INVALID_SOCK INVALID_SOCKET
#define close_socket closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int socket_t;
#define INVALID_SOCK (-1)
#define close_socket close
#endif

#define MAX_FRAME 65536u
int net_init(void);
void net_cleanup(void);
int send_frame(socket_t sock, const char *message);
int recv_frame(socket_t sock, char *out, size_t capacity);
void sleep_ms(unsigned ms);
int valid_field(const char *s, size_t max_len);

#endif
