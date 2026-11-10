#include "net.h"
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#include <signal.h>
#endif

int net_init(void) {
#ifdef _WIN32
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
    signal(SIGPIPE, SIG_IGN);
    return 1;
#endif
}
void net_cleanup(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}
void sleep_ms(unsigned ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec t = {(time_t)(ms / 1000), (long)((ms % 1000) * 1000000)};
    nanosleep(&t, NULL);
#endif
}
static int send_all(socket_t s, const unsigned char *p, size_t n) {
    while (n) {
        int k = send(s, (const char *)p, (int)n, 0);
        if (k <= 0) return 0;
        p += k; n -= (size_t)k;
    }
    return 1;
}
static int recv_all(socket_t s, unsigned char *p, size_t n) {
    while (n) {
        int k = recv(s, (char *)p, (int)n, 0);
        if (k <= 0) return 0;
        p += k; n -= (size_t)k;
    }
    return 1;
}
int send_frame(socket_t s, const char *message) {
    size_t n = strlen(message);
    if (n > MAX_FRAME) return 0;
    uint32_t len = htonl((uint32_t)n);
    return send_all(s, (const unsigned char *)&len, 4) &&
           send_all(s, (const unsigned char *)message, n);
}
int recv_frame(socket_t s, char *out, size_t capacity) {
    uint32_t len;
    if (!recv_all(s, (unsigned char *)&len, 4)) return 0;
    len = ntohl(len);
    if (len >= capacity || len > MAX_FRAME) return 0;
    if (!recv_all(s, (unsigned char *)out, len)) return 0;
    out[len] = 0;
    return 1;
}
int valid_field(const char *s, size_t max_len) {
    size_t n;
    if (!s || !(n = strlen(s)) || n > max_len) return 0;
    for (size_t i = 0; i < n; ++i)
        if (s[i] == '|' || s[i] == '\n' || s[i] == '\r' || (unsigned char)s[i] < 32) return 0;
    return 1;
}
