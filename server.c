#include "net.h"
#include "password.h"
#include "seed_questions.h"
#include <sqlite3.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <ctype.h>
#ifndef _WIN32
#include <sys/select.h>
#endif

#define MAX_USERS 32
#define MAX_QUESTIONS 50
#define MAX_DURATION 86400

static sqlite3 *db;
static pthread_mutex_t db_mutex = PTHREAD_MUTEX_INITIALIZER;
static const char *admin_code;
static volatile int running = 1;

typedef struct {
    socket_t sock;
    int user_id;
    char username[64];
    char role[16];
    pthread_mutex_t send_mutex;
} Client;
static Client *active_clients[256];
static int active_count;

/* Called while db_mutex is held, as are updates to the active client list. */
static int has_active_session(int user_id, const Client *current) {
    for (int i = 0; i < active_count; ++i)
        if (active_clients[i] != current && active_clients[i]->user_id == user_id) return 1;
    return 0;
}

/* Notice a closed peer while WAIT is long-polling, so its session is released promptly. */
static int peer_closed(socket_t s) {
    fd_set ready_set;
    FD_ZERO(&ready_set);
    FD_SET(s, &ready_set);
    struct timeval timeout = {0, 0};
#ifdef _WIN32
    int ready = select(0, &ready_set, NULL, NULL, &timeout);
#else
    int ready = select(s + 1, &ready_set, NULL, NULL, &timeout);
#endif
    if (ready <= 0) return 0;
    char byte;
    return recv(s, &byte, 1, MSG_PEEK) == 0;
}

static int send_to_client(Client *c, const char *message) {
    pthread_mutex_lock(&c->send_mutex);
    int ok = send_frame(c->sock, message);
    pthread_mutex_unlock(&c->send_mutex);
    return ok;
}

static int sql(const char *statement) {
    char *err = NULL;
    int ok = sqlite3_exec(db, statement, NULL, NULL, &err) == SQLITE_OK;
    if (!ok) { fprintf(stderr, "SQLite: %s\n", err ? err : "unknown"); sqlite3_free(err); }
    return ok;
}
static int prep(sqlite3_stmt **s, const char *q) {
    return sqlite3_prepare_v2(db, q, -1, s, NULL) == SQLITE_OK;
}
static int scalar(sqlite3_stmt *s) {
    int value = sqlite3_step(s) == SQLITE_ROW ? sqlite3_column_int(s, 0) : 0;
    sqlite3_finalize(s);
    return value;
}
static void log_event(int user, const char *event, const char *detail) {
    sqlite3_stmt *s;
    if (!prep(&s, "INSERT INTO logs(user_id,event,detail) VALUES(NULLIF(?,0),?,?)")) return;
    sqlite3_bind_int(s, 1, user);
    sqlite3_bind_text(s, 2, event, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 3, detail ? detail : "", -1, SQLITE_TRANSIENT);
    sqlite3_step(s); sqlite3_finalize(s);
}
static void append(char *out, size_t cap, const char *fmt, ...) {
    size_t n = strlen(out);
    if (n >= cap) return;
    va_list args; va_start(args, fmt);
    vsnprintf(out + n, cap - n, fmt, args);
    va_end(args);
}
static int number(const char *s, int min, int max, int *value) {
    if (!s || !*s) return 0;
    char *end; long x = strtol(s, &end, 10);
    if (*end || x < min || x > max) return 0;
    *value = (int)x; return 1;
}
static int fields(char *msg, char **v, int max) {
    int n = 0;
    v[n++] = msg;
    for (char *p = msg; *p; ++p) {
        if (*p == '|') {
            *p = 0;
            if (n == max) return -1;
            v[n++] = p + 1;
        }
    }
    return n;
}
static int init_db(const char *path) {
    if (sqlite3_open(path, &db) != SQLITE_OK) return 0;
    sqlite3_busy_timeout(db, 3000);
    return sql("PRAGMA foreign_keys=ON;") &&
      sql("CREATE TABLE IF NOT EXISTS users(id INTEGER PRIMARY KEY,username TEXT UNIQUE NOT NULL,password TEXT NOT NULL,role TEXT NOT NULL CHECK(role IN ('admin','student')));") &&
      sql("CREATE TABLE IF NOT EXISTS questions(id INTEGER PRIMARY KEY,text TEXT NOT NULL,a TEXT NOT NULL,b TEXT NOT NULL,c TEXT NOT NULL,d TEXT NOT NULL,correct TEXT NOT NULL,topic TEXT NOT NULL,difficulty TEXT NOT NULL);") &&
      sql("CREATE TABLE IF NOT EXISTS rooms(id INTEGER PRIMARY KEY,name TEXT UNIQUE NOT NULL,owner_id INTEGER NOT NULL,count INTEGER NOT NULL,duration INTEGER NOT NULL,state TEXT NOT NULL DEFAULT 'WAITING' CHECK(state IN ('WAITING','RUNNING','FINISHED')),start_at INTEGER,end_at INTEGER,FOREIGN KEY(owner_id) REFERENCES users(id));") &&
      sql("CREATE TABLE IF NOT EXISTS room_questions(room_id INTEGER NOT NULL,position INTEGER NOT NULL,question_id INTEGER NOT NULL,PRIMARY KEY(room_id,position),FOREIGN KEY(room_id) REFERENCES rooms(id),FOREIGN KEY(question_id) REFERENCES questions(id));") &&
      sql("CREATE TABLE IF NOT EXISTS enrollments(room_id INTEGER NOT NULL,user_id INTEGER NOT NULL,joined_at INTEGER NOT NULL,PRIMARY KEY(room_id,user_id),FOREIGN KEY(room_id) REFERENCES rooms(id),FOREIGN KEY(user_id) REFERENCES users(id));") &&
      sql("CREATE TABLE IF NOT EXISTS exam_answers(room_id INTEGER NOT NULL,user_id INTEGER NOT NULL,position INTEGER NOT NULL,selected TEXT NOT NULL,PRIMARY KEY(room_id,user_id,position),FOREIGN KEY(room_id,user_id) REFERENCES enrollments(room_id,user_id));") &&
      sql("CREATE TABLE IF NOT EXISTS results(room_id INTEGER NOT NULL,user_id INTEGER NOT NULL,score INTEGER NOT NULL,total INTEGER NOT NULL,submitted_at INTEGER NOT NULL,automatic INTEGER NOT NULL,PRIMARY KEY(room_id,user_id),FOREIGN KEY(room_id,user_id) REFERENCES enrollments(room_id,user_id));") &&
      sql("CREATE TABLE IF NOT EXISTS practices(id INTEGER PRIMARY KEY,user_id INTEGER NOT NULL,count INTEGER NOT NULL,duration INTEGER NOT NULL,end_at INTEGER NOT NULL,score INTEGER,submitted_at INTEGER,FOREIGN KEY(user_id) REFERENCES users(id));") &&
      sql("CREATE TABLE IF NOT EXISTS practice_questions(practice_id INTEGER NOT NULL,position INTEGER NOT NULL,question_id INTEGER NOT NULL,PRIMARY KEY(practice_id,position),FOREIGN KEY(practice_id) REFERENCES practices(id),FOREIGN KEY(question_id) REFERENCES questions(id));") &&
      sql("CREATE TABLE IF NOT EXISTS practice_answers(practice_id INTEGER NOT NULL,position INTEGER NOT NULL,selected TEXT NOT NULL,PRIMARY KEY(practice_id,position),FOREIGN KEY(practice_id) REFERENCES practices(id));") &&
      sql("CREATE TABLE IF NOT EXISTS logs(id INTEGER PRIMARY KEY,user_id INTEGER,event TEXT NOT NULL,detail TEXT NOT NULL,at INTEGER NOT NULL DEFAULT (strftime('%s','now')),FOREIGN KEY(user_id) REFERENCES users(id));") &&
      seed_questions(db);
}

static int room_id(const char *name) {
    sqlite3_stmt *s;
    if (!prep(&s, "SELECT id FROM rooms WHERE name=?")) return 0;
    sqlite3_bind_text(s, 1, name, -1, SQLITE_TRANSIENT);
    return scalar(s);
}
static int member(int room, int user) {
    sqlite3_stmt *s;
    if (!prep(&s, "SELECT 1 FROM enrollments WHERE room_id=? AND user_id=?")) return 0;
    sqlite3_bind_int(s, 1, room); sqlite3_bind_int(s, 2, user);
    return scalar(s);
}
static int owner(int room, int user) {
    sqlite3_stmt *s;
    if (!prep(&s, "SELECT 1 FROM rooms WHERE id=? AND owner_id=?")) return 0;
    sqlite3_bind_int(s, 1, room); sqlite3_bind_int(s, 2, user);
    return scalar(s);
}
static int question_count(void) {
    sqlite3_stmt *s;
    if (!prep(&s, "SELECT COUNT(*) FROM questions")) return 0;
    return scalar(s);
}
static int room_score(int room, int user) {
    sqlite3_stmt *s;
    if (!prep(&s, "SELECT COUNT(*) FROM room_questions rq JOIN questions q ON q.id=rq.question_id JOIN exam_answers a ON a.room_id=rq.room_id AND a.position=rq.position AND a.user_id=? WHERE rq.room_id=? AND a.selected=q.correct")) return 0;
    sqlite3_bind_int(s, 1, user); sqlite3_bind_int(s, 2, room);
    return scalar(s);
}
static int submit_user(int room, int user, int automatic, time_t now) {
    sqlite3_stmt *s;
    if (!prep(&s, "INSERT OR IGNORE INTO results(room_id,user_id,score,total,submitted_at,automatic) SELECT id,?,?,count,?,? FROM rooms WHERE id=?")) return 0;
    int score = room_score(room, user);
    sqlite3_bind_int(s, 1, user); sqlite3_bind_int(s, 2, score);
    sqlite3_bind_int64(s, 3, (sqlite3_int64)now); sqlite3_bind_int(s, 4, automatic);
    sqlite3_bind_int(s, 5, room);
    int ok = sqlite3_step(s) == SQLITE_DONE;
    int added = sqlite3_changes(db);
    sqlite3_finalize(s);
    if (ok && added) log_event(user, automatic ? "AUTO_SUBMIT" : "SUBMIT", "exam");
    return ok;
}
/* Called with db_mutex held; the registry and session user IDs use the same lock. */
static void notify_auto_score(int room, int user, int score) {
    sqlite3_stmt *s;
    if (!prep(&s, "SELECT name,count FROM rooms WHERE id=?")) return;
    sqlite3_bind_int(s, 1, room);
    if (sqlite3_step(s) == SQLITE_ROW) {
        char message[160];
        snprintf(message, sizeof(message), "EVENT|AUTO_SCORE|%s|%d|%d",
                 sqlite3_column_text(s, 0), score, sqlite3_column_int(s, 1));
        for (int i = 0; i < active_count; ++i)
            if (active_clients[i]->user_id == user) send_to_client(active_clients[i], message);
    }
    sqlite3_finalize(s);
}
static void finish_room_if_done(int room) {
    sqlite3_stmt *s;
    if (!prep(&s, "SELECT (SELECT COUNT(*) FROM enrollments WHERE room_id=?)-(SELECT COUNT(*) FROM results WHERE room_id=?)")) return;
    sqlite3_bind_int(s, 1, room); sqlite3_bind_int(s, 2, room);
    if (scalar(s) == 0) {
        sqlite3_stmt *u;
        if (prep(&u, "UPDATE rooms SET state='FINISHED' WHERE id=? AND state='RUNNING'")) {
            sqlite3_bind_int(u, 1, room); sqlite3_step(u); sqlite3_finalize(u);
            if (sqlite3_changes(db)) log_event(0, "FINISH_ROOM", "all submitted");
        }
    }
}
static void finish_expired(void) {
    time_t now = time(NULL);
    sqlite3_stmt *s;
    if (prep(&s, "SELECT id FROM rooms WHERE state='RUNNING' AND end_at<=?")) {
        sqlite3_bind_int64(s, 1, (sqlite3_int64)now);
        int ids[1000], count = 0;
        while (sqlite3_step(s) == SQLITE_ROW && count < 1000) ids[count++] = sqlite3_column_int(s, 0);
        sqlite3_finalize(s);
        for (int i = 0; i < count; ++i) {
            int room = ids[i];
            sqlite3_stmt *p;
            if (prep(&p, "SELECT e.user_id FROM enrollments e LEFT JOIN results r ON r.room_id=e.room_id AND r.user_id=e.user_id WHERE e.room_id=? AND r.user_id IS NULL")) {
                sqlite3_bind_int(p, 1, room);
                int users[MAX_USERS], n = 0;
                while (sqlite3_step(p) == SQLITE_ROW && n < MAX_USERS) users[n++] = sqlite3_column_int(p, 0);
                sqlite3_finalize(p);
                for (int j = 0; j < n; ++j) {
                    int score = room_score(room, users[j]);
                    if (submit_user(room, users[j], 1, now)) notify_auto_score(room, users[j], score);
                }
            }
            if (prep(&p, "UPDATE rooms SET state='FINISHED' WHERE id=? AND state='RUNNING'")) {
                sqlite3_bind_int(p, 1, room); sqlite3_step(p); sqlite3_finalize(p);
                log_event(0, "FINISH_ROOM", "time expired");
            }
        }
    }
    if (prep(&s, "SELECT id FROM practices WHERE score IS NULL AND end_at<=?")) {
        sqlite3_bind_int64(s, 1, (sqlite3_int64)now);
        int ids[1000], count = 0;
        while (sqlite3_step(s) == SQLITE_ROW && count < 1000) ids[count++] = sqlite3_column_int(s, 0);
        sqlite3_finalize(s);
        for (int i = 0; i < count; ++i) {
            sqlite3_stmt *u;
            if (prep(&u, "UPDATE practices SET score=(SELECT COUNT(*) FROM practice_questions pq JOIN questions q ON q.id=pq.question_id JOIN practice_answers a ON a.practice_id=pq.practice_id AND a.position=pq.position WHERE pq.practice_id=? AND a.selected=q.correct),submitted_at=? WHERE id=? AND score IS NULL")) {
                sqlite3_bind_int(u, 1, ids[i]); sqlite3_bind_int64(u, 2, (sqlite3_int64)now); sqlite3_bind_int(u, 3, ids[i]);
                sqlite3_step(u); sqlite3_finalize(u);
                log_event(0, "AUTO_SUBMIT_PRACTICE", "time expired");
            }
        }
    }
}

static void exam_payload(int room, int user, const char *name, char *out, size_t cap) {
    sqlite3_stmt *s;
    if (!prep(&s, "SELECT end_at,count FROM rooms WHERE id=? AND state='RUNNING'")) { strcpy(out, "ERR|DB"); return; }
    sqlite3_bind_int(s, 1, room);
    if (sqlite3_step(s) != SQLITE_ROW) { sqlite3_finalize(s); strcpy(out, "ERR|NOT_RUNNING"); return; }
    long long end_at = sqlite3_column_int64(s, 0);
    int count = sqlite3_column_int(s, 1);
    sqlite3_finalize(s);
    snprintf(out, cap, "EXAM|%s|%lld|%d", name, end_at, count);
    if (!prep(&s, "SELECT rq.position,q.text,q.a,q.b,q.c,q.d,COALESCE(ea.selected,'') FROM room_questions rq JOIN questions q ON q.id=rq.question_id LEFT JOIN exam_answers ea ON ea.room_id=rq.room_id AND ea.position=rq.position AND ea.user_id=? WHERE rq.room_id=? ORDER BY rq.position")) return;
    sqlite3_bind_int(s, 1, user); sqlite3_bind_int(s, 2, room);
    while (sqlite3_step(s) == SQLITE_ROW)
        append(out, cap, "\nQ|%d|%s|%s|%s|%s|%s|%s", sqlite3_column_int(s, 0),
               sqlite3_column_text(s, 1), sqlite3_column_text(s, 2),
               sqlite3_column_text(s, 3), sqlite3_column_text(s, 4), sqlite3_column_text(s, 5),
               sqlite3_column_text(s, 6));
    sqlite3_finalize(s);
}

static void dispatch(Client *c, char **v, int n, char *out, size_t cap) {
    sqlite3_stmt *s;
    const char *cmd = v[0];
    strcpy(out, "ERR|BAD_REQUEST");
    if (!strcmp(cmd, "REGISTER")) {
        if (n != 5 || c->user_id || !valid_field(v[1], 32) || !valid_field(v[2], 128) ||
            (strcmp(v[3], "admin") && strcmp(v[3], "student"))) return;
        if (!strcmp(v[3], "admin") && strcmp(v[4], admin_code)) { strcpy(out, "ERR|ADMIN_CODE"); return; }
        char password_value[128];
        if (!password_hash(v[2], password_value)) { strcpy(out, "ERR|PASSWORD_HASH"); return; }
        if (!prep(&s, "INSERT INTO users(username,password,role) VALUES(?,?,?)")) { strcpy(out, "ERR|DB"); return; }
        sqlite3_bind_text(s, 1, v[1], -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 2, password_value, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 3, v[3], -1, SQLITE_TRANSIENT);
        int ok = sqlite3_step(s) == SQLITE_DONE;
        sqlite3_finalize(s);
        if (!ok) { strcpy(out, "ERR|USER_EXISTS"); return; }
        log_event((int)sqlite3_last_insert_rowid(db), "REGISTER", v[3]);
        strcpy(out, "OK|REGISTERED"); return;
    }
    if (!strcmp(cmd, "LOGIN")) {
        if (n != 3 || c->user_id) return;
        if (!prep(&s, "SELECT id,role,password FROM users WHERE username=?")) { strcpy(out, "ERR|DB"); return; }
        sqlite3_bind_text(s, 1, v[1], -1, SQLITE_TRANSIENT);
        if (sqlite3_step(s) == SQLITE_ROW && password_verify(v[2], (const char *)sqlite3_column_text(s, 2))) {
            int user_id = sqlite3_column_int(s, 0);
            if (has_active_session(user_id, c)) {
                strcpy(out, "ERR|ALREADY_LOGGED_IN");
                log_event(user_id, "LOGIN_REJECTED_ACTIVE", "already connected");
            } else {
                c->user_id = user_id;
                snprintf(c->role, sizeof(c->role), "%s", sqlite3_column_text(s, 1));
                snprintf(c->username, sizeof(c->username), "%s", v[1]);
                snprintf(out, cap, "OK|LOGIN|%s", c->role);
                log_event(c->user_id, "LOGIN", "success");
            }
        } else { strcpy(out, "ERR|CREDENTIALS"); log_event(0, "LOGIN_FAILED", v[1]); }
        sqlite3_finalize(s); return;
    }
    if (!c->user_id) { strcpy(out, "ERR|LOGIN_REQUIRED"); return; }
    if (!strcmp(cmd, "LOGOUT")) {
        if (n != 1) return;
        log_event(c->user_id, "LOGOUT", "");
        c->user_id = 0; c->username[0] = 0; c->role[0] = 0;
        strcpy(out, "OK|LOGOUT"); return;
    }
    if (!strcmp(cmd, "CHPASS")) {
        if (n != 3 || !valid_field(v[2], 128)) return;
        if (!prep(&s, "SELECT password FROM users WHERE id=?")) { strcpy(out, "ERR|DB"); return; }
        sqlite3_bind_int(s, 1, c->user_id);
        int valid = sqlite3_step(s) == SQLITE_ROW && password_verify(v[1], (const char *)sqlite3_column_text(s, 0));
        sqlite3_finalize(s);
        if (!valid) { strcpy(out, "ERR|OLD_PASSWORD"); return; }
        char password_value[128];
        if (!password_hash(v[2], password_value)) { strcpy(out, "ERR|PASSWORD_HASH"); return; }
        if (!prep(&s, "UPDATE users SET password=? WHERE id=?")) { strcpy(out, "ERR|DB"); return; }
        sqlite3_bind_text(s, 1, password_value, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(s, 2, c->user_id);
        int ok = sqlite3_step(s) == SQLITE_DONE;
        int changed = sqlite3_changes(db);
        sqlite3_finalize(s);
        if (ok && changed) { log_event(c->user_id, "PASSWORD_CHANGE", ""); strcpy(out, "OK|PASSWORD_CHANGED"); }
        else strcpy(out, "ERR|OLD_PASSWORD");
        return;
    }
    if (!strcmp(cmd, "ADDQ")) {
        if (strcmp(c->role, "admin")) { strcpy(out, "ERR|FORBIDDEN"); return; }
        if (n != 9) return;
        for (int i = 1; i < 9; ++i) if (!valid_field(v[i], i == 1 ? 200 : 100)) return;
        if (strlen(v[6]) != 1 || !strchr("ABCD", v[6][0])) return;
        if (!prep(&s, "INSERT INTO questions(text,a,b,c,d,correct,topic,difficulty) VALUES(?,?,?,?,?,?,?,?)")) { strcpy(out, "ERR|DB"); return; }
        for (int i = 1; i < 9; ++i) sqlite3_bind_text(s, i, v[i], -1, SQLITE_TRANSIENT);
        int ok = sqlite3_step(s) == SQLITE_DONE; sqlite3_finalize(s);
        if (!ok) { strcpy(out, "ERR|DB"); return; }
        log_event(c->user_id, "ADD_QUESTION", v[1]);
        snprintf(out, cap, "OK|QUESTION|%lld", (long long)sqlite3_last_insert_rowid(db)); return;
    }
    if (!strcmp(cmd, "CREATE")) {
        int count, duration;
        if (n != 4 || !valid_field(v[1], 48) || !number(v[2], 1, MAX_QUESTIONS, &count) ||
            !number(v[3], 10, MAX_DURATION, &duration)) return;
        if (question_count() < count) { strcpy(out, "ERR|NOT_ENOUGH_QUESTIONS"); return; }
        if (!sql("BEGIN IMMEDIATE")) { strcpy(out, "ERR|DB"); return; }
        if (!prep(&s, "INSERT INTO rooms(name,owner_id,count,duration) VALUES(?,?,?,?)")) { sql("ROLLBACK"); strcpy(out, "ERR|DB"); return; }
        sqlite3_bind_text(s, 1, v[1], -1, SQLITE_TRANSIENT); sqlite3_bind_int(s, 2, c->user_id);
        sqlite3_bind_int(s, 3, count); sqlite3_bind_int(s, 4, duration);
        int ok = sqlite3_step(s) == SQLITE_DONE; sqlite3_finalize(s);
        if (!ok) { sql("ROLLBACK"); strcpy(out, "ERR|ROOM_EXISTS"); return; }
        int room = (int)sqlite3_last_insert_rowid(db);
        if (!prep(&s, "INSERT INTO room_questions(room_id,position,question_id) SELECT ?,row_number() OVER (ORDER BY random()),id FROM (SELECT id FROM questions ORDER BY random() LIMIT ?)")) {
            sql("ROLLBACK"); strcpy(out, "ERR|DB"); return;
        }
        sqlite3_bind_int(s, 1, room); sqlite3_bind_int(s, 2, count);
        ok = sqlite3_step(s) == SQLITE_DONE; sqlite3_finalize(s);
        if (!ok || !sql("COMMIT")) { sql("ROLLBACK"); strcpy(out, "ERR|DB"); return; }
        log_event(c->user_id, "CREATE_ROOM", v[1]);
        strcpy(out, "OK|ROOM|WAITING"); return;
    }
    if (!strcmp(cmd, "LIST")) {
        if (n != 1) return;
        strcpy(out, "ROOMS");
        if (!prep(&s, "SELECT name,state,count,duration,(SELECT username FROM users WHERE id=owner_id) FROM rooms ORDER BY id DESC LIMIT 100")) { strcpy(out, "ERR|DB"); return; }
        while (sqlite3_step(s) == SQLITE_ROW)
            append(out, cap, "\nR|%s|%s|%d|%d|%s", sqlite3_column_text(s, 0), sqlite3_column_text(s, 1),
                   sqlite3_column_int(s, 2), sqlite3_column_int(s, 3), sqlite3_column_text(s, 4));
        sqlite3_finalize(s); return;
    }
    if (!strcmp(cmd, "JOIN")) {
        if (n != 2) return;
        int room = room_id(v[1]);
        if (!room) { strcpy(out, "ERR|ROOM_NOT_FOUND"); return; }
        if (!prep(&s, "SELECT state FROM rooms WHERE id=?")) { strcpy(out, "ERR|DB"); return; }
        sqlite3_bind_int(s, 1, room);
        const char *state = sqlite3_step(s) == SQLITE_ROW ? (const char *)sqlite3_column_text(s, 0) : "";
        int waiting = !strcmp(state, "WAITING"); sqlite3_finalize(s);
        if (!waiting) { strcpy(out, "ERR|ROOM_NOT_WAITING"); return; }
        if (member(room, c->user_id)) { strcpy(out, "OK|JOINED"); return; }
        if (!prep(&s, "SELECT COUNT(*) FROM enrollments WHERE room_id=?")) { strcpy(out, "ERR|DB"); return; }
        sqlite3_bind_int(s, 1, room);
        if (scalar(s) >= MAX_USERS) { strcpy(out, "ERR|ROOM_FULL"); return; }
        if (!prep(&s, "INSERT INTO enrollments(room_id,user_id,joined_at) VALUES(?,?,?)")) { strcpy(out, "ERR|DB"); return; }
        sqlite3_bind_int(s, 1, room); sqlite3_bind_int(s, 2, c->user_id);
        sqlite3_bind_int64(s, 3, (sqlite3_int64)time(NULL));
        int ok = sqlite3_step(s) == SQLITE_DONE; sqlite3_finalize(s);
        if (!ok) { strcpy(out, "ERR|DB"); return; }
        log_event(c->user_id, "JOIN_ROOM", v[1]);
        strcpy(out, "OK|JOINED"); return;
    }
    if (!strcmp(cmd, "START")) {
        if (n != 2) return;
        int room = room_id(v[1]);
        if (!room) { strcpy(out, "ERR|ROOM_NOT_FOUND"); return; }
        if (!owner(room, c->user_id)) { strcpy(out, "ERR|FORBIDDEN"); return; }
        if (!prep(&s, "SELECT COUNT(*) FROM enrollments WHERE room_id=?")) { strcpy(out, "ERR|DB"); return; }
        sqlite3_bind_int(s, 1, room);
        if (!scalar(s)) { strcpy(out, "ERR|NO_PARTICIPANTS"); return; }
        if (!prep(&s, "UPDATE rooms SET state='RUNNING',start_at=?,end_at=?+duration WHERE id=? AND state='WAITING'")) { strcpy(out, "ERR|DB"); return; }
        sqlite3_bind_int64(s, 1, (sqlite3_int64)time(NULL)); sqlite3_bind_int64(s, 2, (sqlite3_int64)time(NULL)); sqlite3_bind_int(s, 3, room);
        int ok = sqlite3_step(s) == SQLITE_DONE; int changed = sqlite3_changes(db); sqlite3_finalize(s);
        if (!ok || !changed) { strcpy(out, "ERR|ROOM_NOT_WAITING"); return; }
        log_event(c->user_id, "START_ROOM", v[1]);
        strcpy(out, "OK|STARTED"); return;
    }
    if (!strcmp(cmd, "EXAM") || !strcmp(cmd, "WAIT")) {
        if (n != 2) return;
        int room = room_id(v[1]);
        if (!room) { strcpy(out, "ERR|ROOM_NOT_FOUND"); return; }
        if (!member(room, c->user_id)) { strcpy(out, "ERR|NOT_PARTICIPANT"); return; }
        if (!prep(&s, "SELECT state FROM rooms WHERE id=?")) { strcpy(out, "ERR|DB"); return; }
        sqlite3_bind_int(s, 1, room);
        const char *state = sqlite3_step(s) == SQLITE_ROW ? (const char *)sqlite3_column_text(s, 0) : "";
        char copy[16]; snprintf(copy, sizeof(copy), "%s", state); sqlite3_finalize(s);
        if (!strcmp(copy, "WAITING")) { strcpy(out, "WAITING"); return; }
        if (!strcmp(copy, "RUNNING")) { exam_payload(room, c->user_id, v[1], out, cap); return; }
        if (!prep(&s, "SELECT score,total FROM results WHERE room_id=? AND user_id=?")) { strcpy(out, "ERR|DB"); return; }
        sqlite3_bind_int(s, 1, room); sqlite3_bind_int(s, 2, c->user_id);
        if (sqlite3_step(s) == SQLITE_ROW) snprintf(out, cap, "FINISHED|%d|%d", sqlite3_column_int(s, 0), sqlite3_column_int(s, 1));
        else strcpy(out, "ERR|RESULT_NOT_READY");
        sqlite3_finalize(s); return;
    }
    if (!strcmp(cmd, "ANSWER")) {
        int position;
        if (n != 4 || !number(v[2], 1, MAX_QUESTIONS, &position) || strlen(v[3]) != 1 || !strchr("ABCD", v[3][0])) return;
        int room = room_id(v[1]);
        if (!room || !member(room, c->user_id)) { strcpy(out, "ERR|NOT_PARTICIPANT"); return; }
        if (!prep(&s, "SELECT COUNT(*) FROM rooms WHERE id=? AND state='RUNNING' AND end_at>? AND count>=? AND NOT EXISTS(SELECT 1 FROM results WHERE room_id=? AND user_id=?)")) { strcpy(out, "ERR|DB"); return; }
        sqlite3_bind_int(s, 1, room); sqlite3_bind_int64(s, 2, (sqlite3_int64)time(NULL)); sqlite3_bind_int(s, 3, position);
        sqlite3_bind_int(s, 4, room); sqlite3_bind_int(s, 5, c->user_id);
        if (!scalar(s)) { strcpy(out, "ERR|EXAM_CLOSED"); return; }
        if (!prep(&s, "INSERT INTO exam_answers(room_id,user_id,position,selected) VALUES(?,?,?,?) ON CONFLICT(room_id,user_id,position) DO UPDATE SET selected=excluded.selected")) { strcpy(out, "ERR|DB"); return; }
        sqlite3_bind_int(s, 1, room); sqlite3_bind_int(s, 2, c->user_id); sqlite3_bind_int(s, 3, position);
        sqlite3_bind_text(s, 4, v[3], -1, SQLITE_TRANSIENT);
        int ok = sqlite3_step(s) == SQLITE_DONE; sqlite3_finalize(s);
        strcpy(out, ok ? "OK|ANSWER_SAVED" : "ERR|DB"); return;
    }
    if (!strcmp(cmd, "SUBMIT")) {
        if (n != 2) return;
        int room = room_id(v[1]);
        if (!room || !member(room, c->user_id)) { strcpy(out, "ERR|NOT_PARTICIPANT"); return; }
        if (!prep(&s, "SELECT score,total FROM results WHERE room_id=? AND user_id=?")) { strcpy(out, "ERR|DB"); return; }
        sqlite3_bind_int(s, 1, room); sqlite3_bind_int(s, 2, c->user_id);
        if (sqlite3_step(s) == SQLITE_ROW) {
            snprintf(out, cap, "SCORE|%d|%d", sqlite3_column_int(s, 0), sqlite3_column_int(s, 1));
            sqlite3_finalize(s); return;
        }
        sqlite3_finalize(s);
        if (!prep(&s, "SELECT state,end_at,count FROM rooms WHERE id=?")) { strcpy(out, "ERR|DB"); return; }
        sqlite3_bind_int(s, 1, room);
        if (sqlite3_step(s) != SQLITE_ROW) { sqlite3_finalize(s); strcpy(out, "ERR|ROOM_NOT_FOUND"); return; }
        int active = !strcmp((const char *)sqlite3_column_text(s, 0), "RUNNING") &&
                     sqlite3_column_int64(s, 1) > (sqlite3_int64)time(NULL);
        int total = sqlite3_column_int(s, 2); sqlite3_finalize(s);
        if (!active) { strcpy(out, "ERR|EXAM_CLOSED"); return; }
        int score = room_score(room, c->user_id);
        if (!submit_user(room, c->user_id, 0, time(NULL))) { strcpy(out, "ERR|DB"); return; }
        finish_room_if_done(room);
        snprintf(out, cap, "SCORE|%d|%d", score, total); return;
    }
    if (!strcmp(cmd, "STATUS")) {
        if (n != 2) return;
        int room = room_id(v[1]);
        if (!room || !member(room, c->user_id)) { strcpy(out, "ERR|NOT_PARTICIPANT"); return; }
        if (!prep(&s, "SELECT state,end_at,count FROM rooms WHERE id=?")) { strcpy(out, "ERR|DB"); return; }
        sqlite3_bind_int(s, 1, room);
        if (sqlite3_step(s) != SQLITE_ROW) { sqlite3_finalize(s); strcpy(out, "ERR|DB"); return; }
        char state[16]; snprintf(state, sizeof(state), "%s", sqlite3_column_text(s, 0));
        long long end = sqlite3_column_int64(s, 1); int total = sqlite3_column_int(s, 2);
        sqlite3_finalize(s);
        if (prep(&s, "SELECT score FROM results WHERE room_id=? AND user_id=?")) {
            sqlite3_bind_int(s, 1, room); sqlite3_bind_int(s, 2, c->user_id);
            if (sqlite3_step(s) == SQLITE_ROW) snprintf(out, cap, "SCORE|%d|%d", sqlite3_column_int(s, 0), total);
            else snprintf(out, cap, "STATUS|%s|%lld", state, end);
            sqlite3_finalize(s);
        }
        return;
    }
    if (!strcmp(cmd, "RESULTS")) {
        if (n != 2) return;
        int room = room_id(v[1]);
        if (!room) { strcpy(out, "ERR|ROOM_NOT_FOUND"); return; }
        if (!owner(room, c->user_id) && !member(room, c->user_id)) { strcpy(out, "ERR|FORBIDDEN"); return; }
        if (!prep(&s, "SELECT COUNT(*) FROM rooms WHERE id=? AND state='FINISHED'")) { strcpy(out, "ERR|DB"); return; }
        sqlite3_bind_int(s, 1, room);
        if (!scalar(s)) { strcpy(out, "ERR|ROOM_NOT_FINISHED"); return; }
        strcpy(out, "RESULTS");
        if (!prep(&s, "SELECT u.username,r.score,r.total,r.automatic FROM results r JOIN users u ON u.id=r.user_id WHERE r.room_id=? ORDER BY r.score DESC,u.username")) { strcpy(out, "ERR|DB"); return; }
        sqlite3_bind_int(s, 1, room);
        while (sqlite3_step(s) == SQLITE_ROW)
            append(out, cap, "\nR|%s|%d|%d|%d", sqlite3_column_text(s, 0), sqlite3_column_int(s, 1),
                   sqlite3_column_int(s, 2), sqlite3_column_int(s, 3));
        sqlite3_finalize(s); return;
    }
    if (!strcmp(cmd, "PRACTICE")) {
        int count, duration;
        if (n != 3 || !number(v[1], 1, MAX_QUESTIONS, &count) || !number(v[2], 10, MAX_DURATION, &duration)) return;
        if (question_count() < count) { strcpy(out, "ERR|NOT_ENOUGH_QUESTIONS"); return; }
        if (!sql("BEGIN IMMEDIATE")) { strcpy(out, "ERR|DB"); return; }
        if (!prep(&s, "INSERT INTO practices(user_id,count,duration,end_at) VALUES(?,?,?,?)")) { sql("ROLLBACK"); strcpy(out, "ERR|DB"); return; }
        sqlite3_bind_int(s, 1, c->user_id); sqlite3_bind_int(s, 2, count); sqlite3_bind_int(s, 3, duration);
        time_t end_at = time(NULL) + duration; sqlite3_bind_int64(s, 4, (sqlite3_int64)end_at);
        int ok = sqlite3_step(s) == SQLITE_DONE; sqlite3_finalize(s);
        if (!ok) { sql("ROLLBACK"); strcpy(out, "ERR|DB"); return; }
        int id = (int)sqlite3_last_insert_rowid(db);
        if (!prep(&s, "INSERT INTO practice_questions(practice_id,position,question_id) SELECT ?,row_number() OVER (ORDER BY random()),id FROM (SELECT id FROM questions ORDER BY random() LIMIT ?)")) { sql("ROLLBACK"); strcpy(out, "ERR|DB"); return; }
        sqlite3_bind_int(s, 1, id); sqlite3_bind_int(s, 2, count);
        ok = sqlite3_step(s) == SQLITE_DONE; sqlite3_finalize(s);
        if (!ok || !sql("COMMIT")) { sql("ROLLBACK"); strcpy(out, "ERR|DB"); return; }
        log_event(c->user_id, "START_PRACTICE", "");
        snprintf(out, cap, "PRACTICE|%d|%lld|%d", id, (long long)end_at, count); return;
    }
    if (!strcmp(cmd, "PQUESTION")) {
        int id, position;
        if (n != 3 || !number(v[1], 1, 1000000000, &id) || !number(v[2], 1, MAX_QUESTIONS, &position)) return;
        if (!prep(&s, "SELECT q.text,q.a,q.b,q.c,q.d FROM practices p JOIN practice_questions pq ON pq.practice_id=p.id JOIN questions q ON q.id=pq.question_id WHERE p.id=? AND p.user_id=? AND p.score IS NULL AND p.end_at>? AND pq.position=?")) { strcpy(out, "ERR|DB"); return; }
        sqlite3_bind_int(s, 1, id); sqlite3_bind_int(s, 2, c->user_id);
        sqlite3_bind_int64(s, 3, (sqlite3_int64)time(NULL)); sqlite3_bind_int(s, 4, position);
        if (sqlite3_step(s) == SQLITE_ROW)
            snprintf(out, cap, "Q|%d|%s|%s|%s|%s|%s", position, sqlite3_column_text(s, 0),
                     sqlite3_column_text(s, 1), sqlite3_column_text(s, 2),
                     sqlite3_column_text(s, 3), sqlite3_column_text(s, 4));
        else strcpy(out, "ERR|PRACTICE_CLOSED");
        sqlite3_finalize(s); return;
    }
    if (!strcmp(cmd, "PANSWER")) {
        int id, position;
        if (n != 4 || !number(v[1], 1, 1000000000, &id) || !number(v[2], 1, MAX_QUESTIONS, &position) ||
            strlen(v[3]) != 1 || !strchr("ABCD", v[3][0])) return;
        if (!prep(&s, "SELECT COUNT(*) FROM practices WHERE id=? AND user_id=? AND score IS NULL AND end_at>? AND count>=?")) { strcpy(out, "ERR|DB"); return; }
        sqlite3_bind_int(s, 1, id); sqlite3_bind_int(s, 2, c->user_id);
        sqlite3_bind_int64(s, 3, (sqlite3_int64)time(NULL)); sqlite3_bind_int(s, 4, position);
        if (!scalar(s)) { strcpy(out, "ERR|PRACTICE_CLOSED"); return; }
        if (!prep(&s, "INSERT INTO practice_answers(practice_id,position,selected) VALUES(?,?,?) ON CONFLICT(practice_id,position) DO UPDATE SET selected=excluded.selected")) { strcpy(out, "ERR|DB"); return; }
        sqlite3_bind_int(s, 1, id); sqlite3_bind_int(s, 2, position);
        sqlite3_bind_text(s, 3, v[3], -1, SQLITE_TRANSIENT);
        int ok = sqlite3_step(s) == SQLITE_DONE; sqlite3_finalize(s);
        strcpy(out, ok ? "OK|ANSWER_SAVED" : "ERR|DB"); return;
    }
    if (!strcmp(cmd, "PSUBMIT") || !strcmp(cmd, "PSTATUS")) {
        int id;
        if (n != 2 || !number(v[1], 1, 1000000000, &id)) return;
        if (!prep(&s, "SELECT count,end_at,score FROM practices WHERE id=? AND user_id=?")) { strcpy(out, "ERR|DB"); return; }
        sqlite3_bind_int(s, 1, id); sqlite3_bind_int(s, 2, c->user_id);
        if (sqlite3_step(s) != SQLITE_ROW) { sqlite3_finalize(s); strcpy(out, "ERR|PRACTICE_NOT_FOUND"); return; }
        int count = sqlite3_column_int(s, 0); long long end = sqlite3_column_int64(s, 1);
        int done = sqlite3_column_type(s, 2) != SQLITE_NULL;
        int score = done ? sqlite3_column_int(s, 2) : 0;
        sqlite3_finalize(s);
        if (done) { snprintf(out, cap, "PSCORE|%d|%d", score, count); return; }
        if (!strcmp(cmd, "PSTATUS")) { snprintf(out, cap, "PSTATUS|RUNNING|%lld", end); return; }
        if (end <= (long long)time(NULL)) { finish_expired(); strcpy(out, "ERR|PRACTICE_CLOSED"); return; }
        if (!prep(&s, "UPDATE practices SET score=(SELECT COUNT(*) FROM practice_questions pq JOIN questions q ON q.id=pq.question_id JOIN practice_answers a ON a.practice_id=pq.practice_id AND a.position=pq.position WHERE pq.practice_id=? AND a.selected=q.correct),submitted_at=? WHERE id=? AND score IS NULL")) { strcpy(out, "ERR|DB"); return; }
        sqlite3_bind_int(s, 1, id); sqlite3_bind_int64(s, 2, (sqlite3_int64)time(NULL)); sqlite3_bind_int(s, 3, id);
        int ok = sqlite3_step(s) == SQLITE_DONE; sqlite3_finalize(s);
        if (!ok) { strcpy(out, "ERR|DB"); return; }
        log_event(c->user_id, "SUBMIT_PRACTICE", "");
        if (prep(&s, "SELECT score FROM practices WHERE id=?")) {
            sqlite3_bind_int(s, 1, id);
            snprintf(out, cap, "PSCORE|%d|%d", scalar(s), count);
        }
        return;
    }
    strcpy(out, "ERR|UNKNOWN_COMMAND");
}

static void *monitor_thread(void *arg) {
    (void)arg;
    while (running) {
        sleep_ms(200);
        pthread_mutex_lock(&db_mutex);
        finish_expired();
        pthread_mutex_unlock(&db_mutex);
    }
    return NULL;
}

static void *client_thread(void *arg) {
    Client *c = arg;
    char request[MAX_FRAME + 1], response[MAX_FRAME + 1];
    pthread_mutex_lock(&db_mutex);
    if (active_count == (int)(sizeof(active_clients) / sizeof(active_clients[0]))) {
        pthread_mutex_unlock(&db_mutex);
        close_socket(c->sock); pthread_mutex_destroy(&c->send_mutex); free(c);
        return NULL;
    }
    active_clients[active_count++] = c;
    pthread_mutex_unlock(&db_mutex);
    while (recv_frame(c->sock, request, sizeof(request))) {
        char *v[16]; int n = fields(request, v, 16);
        int wait = n == 2 && !strcmp(v[0], "WAIT");
        int disconnected = 0;
        if (wait) {
            /* Long polling: a participant is notified with the entire exam as soon as START runs. */
            for (int i = 0; i < 300; ++i) {
                if (peer_closed(c->sock)) { disconnected = 1; break; }
                pthread_mutex_lock(&db_mutex);
                finish_expired();
                dispatch(c, v, n, response, sizeof(response));
                pthread_mutex_unlock(&db_mutex);
                if (strcmp(response, "WAITING")) break;
                sleep_ms(100);
            }
        } else {
            pthread_mutex_lock(&db_mutex);
            finish_expired();
            if (n < 0) strcpy(response, "ERR|TOO_MANY_FIELDS");
            else dispatch(c, v, n, response, sizeof(response));
            pthread_mutex_unlock(&db_mutex);
        }
        if (disconnected) break;
        if (!send_to_client(c, response)) break;
    }
    pthread_mutex_lock(&db_mutex);
    if (c->user_id)
        log_event(c->user_id, "DISCONNECT", "");
    for (int i = 0; i < active_count; ++i) {
        if (active_clients[i] == c) {
            active_clients[i] = active_clients[--active_count];
            break;
        }
    }
    pthread_mutex_unlock(&db_mutex);
    close_socket(c->sock); pthread_mutex_destroy(&c->send_mutex); free(c);
    return NULL;
}

int main(int argc, char **argv) {
    int port = 9000;
    if (argc > 1 && !number(argv[1], 1, 65535, &port)) { fprintf(stderr, "Invalid port\n"); return 1; }
    const char *path = argc > 2 ? argv[2] : "exam.db";
    admin_code = getenv("EXAM_ADMIN_CODE");
    if (!admin_code || !*admin_code) admin_code = "admin123";
    if (!net_init() || !init_db(path)) { fprintf(stderr, "Startup failed\n"); return 1; }
    pthread_mutex_lock(&db_mutex); log_event(0, "SERVER_STARTED", ""); pthread_mutex_unlock(&db_mutex);
    socket_t listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == INVALID_SOCK) { fprintf(stderr, "socket failed\n"); return 1; }
    int yes = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
    struct sockaddr_in addr; memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; addr.sin_port = htons((uint16_t)port); addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(listen_sock, 64) != 0) {
        fprintf(stderr, "bind/listen failed on port %d\n", port); close_socket(listen_sock); return 1;
    }
    pthread_t monitor;
    pthread_create(&monitor, NULL, monitor_thread, NULL);
    pthread_detach(monitor);
    printf("Exam server listening on %d; database: %s\n", port, path); fflush(stdout);
    while (1) {
        socket_t s = accept(listen_sock, NULL, NULL);
        if (s == INVALID_SOCK) continue;
        Client *c = calloc(1, sizeof(*c));
        if (!c) { close_socket(s); continue; }
        c->sock = s;
        pthread_mutex_init(&c->send_mutex, NULL);
        pthread_t thread;
        if (pthread_create(&thread, NULL, client_thread, c) != 0) { close_socket(s); pthread_mutex_destroy(&c->send_mutex); free(c); continue; }
        pthread_detach(thread);
    }
    return 0;
}
