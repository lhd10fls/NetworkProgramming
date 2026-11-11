#include "seed_questions.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *text, *a, *b, *c, *d, *correct, *topic, *difficulty;
} SeedQuestion;

static const SeedQuestion starter_questions[] = {
    {"Which protocol provides reliable, ordered delivery?", "UDP", "TCP", "IP", "ARP", "B", "network", "easy"},
    {"Which protocol is connectionless?", "TCP", "TLS", "UDP", "SSH", "C", "network", "easy"},
    {"What does a TCP server call to wait for a new client?", "bind", "listen", "accept", "connect", "C", "socket", "easy"},
    {"Which call associates a socket with a local address?", "bind", "send", "recv", "close", "A", "socket", "easy"},
    {"Which call starts a client connection to a TCP server?", "accept", "listen", "connect", "select", "C", "socket", "easy"},
    {"What does recv return when a TCP peer closes cleanly?", "-2", "0", "1", "The port number", "B", "socket", "medium"},
    {"Why can a single recv call return less data than requested?", "TCP is a byte stream", "SQLite is locked", "The port changed", "The IP expired", "A", "network", "medium"},
    {"What does htonl convert?", "A string to a number", "Host to network byte order", "A hostname to an IP", "A socket to a file", "B", "network", "medium"},
    {"Which address refers to the same computer?", "8.8.8.8", "255.255.255.255", "127.0.0.1", "0.0.0.0", "C", "network", "easy"},
    {"What identifies a service on a host along with its IP address?", "A port", "A table", "A password", "A thread", "A", "network", "easy"},
    {"Which IP version uses 128-bit addresses?", "IPv4", "IPv6", "TCP", "DNS", "B", "network", "easy"},
    {"Which protocol commonly resolves a domain name to an IP address?", "FTP", "SMTP", "DNS", "HTTP", "C", "network", "easy"},
    {"What does a mutex protect in a multithreaded program?", "Shared state", "A network cable", "A disk partition", "A compiler", "A", "concurrency", "medium"},
    {"Which SQL command retrieves rows from a table?", "INSERT", "SELECT", "UPDATE", "DELETE", "B", "database", "easy"},
    {"What does a database transaction help guarantee?", "Atomic changes", "Faster Wi-Fi", "Encrypted TCP", "New ports", "A", "database", "medium"},
    {"Why use SQL parameters instead of joining user text into a query?", "To avoid SQL injection", "To open a socket", "To create threads", "To sort IPs", "A", "database", "medium"},
    {"Why store a password hash with a unique salt?", "To reduce reuse of precomputed attacks", "To reveal the password", "To change the port", "To compress the database", "A", "security", "medium"},
    {"Which of these adds encryption to a TCP connection?", "TLS", "SQLite", "DNS", "ARP", "A", "security", "easy"},
    {"What does the C11 sizeof operator report?", "Bytes occupied by a type or object", "Number of TCP packets", "Length of a C string", "Number of database rows", "A", "c", "easy"},
    {"Which standard C function allocates memory dynamically?", "printf", "fopen", "malloc", "strcmp", "C", "c", "easy"}
};

int seed_questions(sqlite3 *db) {
    const char *skip = getenv("EXAM_SKIP_SEED");
    if (skip && strcmp(skip, "1") == 0) return 1;

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM questions", -1, &stmt, NULL) != SQLITE_OK) return 0;
    int result = sqlite3_step(stmt);
    int count = result == SQLITE_ROW ? sqlite3_column_int(stmt, 0) : -1;
    sqlite3_finalize(stmt);
    if (count < 0) return 0;
    if (count > 0) return 1;

    if (sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, NULL) != SQLITE_OK) return 0;
    const char *sql = "INSERT INTO questions(text,a,b,c,d,correct,topic,difficulty) VALUES(?,?,?,?,?,?,?,?)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        return 0;
    }
    int ok = 1;
    for (size_t i = 0; i < sizeof(starter_questions) / sizeof(starter_questions[0]); ++i) {
        const SeedQuestion *q = &starter_questions[i];
        const char *values[] = {q->text, q->a, q->b, q->c, q->d, q->correct, q->topic, q->difficulty};
        for (int j = 0; j < 8; ++j)
            if (sqlite3_bind_text(stmt, j + 1, values[j], -1, SQLITE_STATIC) != SQLITE_OK) ok = 0;
        if (!ok || sqlite3_step(stmt) != SQLITE_DONE) { ok = 0; break; }
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }
    sqlite3_finalize(stmt);
    if (ok && sqlite3_exec(db, "COMMIT", NULL, NULL, NULL) == SQLITE_OK) return 1;
    sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
    return 0;
}
