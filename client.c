#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <pthread.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/select.h>
#include <unistd.h>
#endif

static socket_t sock;
static char response[MAX_FRAME + 1];
static char username[64];
static char role[16];
static pthread_mutex_t response_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t response_cond = PTHREAD_COND_INITIALIZER;
static pthread_t receiver_id;
static int awaiting_response, response_ready, connection_alive = 1;
static int stdin_closed;

static void *receiver_thread(void *arg) {
    (void)arg;
    char incoming[MAX_FRAME + 1];
    while (recv_frame(sock, incoming, sizeof(incoming))) {
        if (!strncmp(incoming, "EVENT|AUTO_SCORE|", 17)) {
            char room[64]; int score, total;
            if (sscanf(incoming, "EVENT|AUTO_SCORE|%63[^|]|%d|%d", room, &score, &total) == 3) {
                printf("\n[Server] Time is up in %s. Your score: %d/%d\n", room, score, total);
                fflush(stdout);
            }
            continue;
        }
        pthread_mutex_lock(&response_mutex);
        if (awaiting_response) {
            memcpy(response, incoming, strlen(incoming) + 1);
            response_ready = 1;
            pthread_cond_signal(&response_cond);
        }
        pthread_mutex_unlock(&response_mutex);
    }
    pthread_mutex_lock(&response_mutex);
    connection_alive = 0;
    pthread_cond_broadcast(&response_cond);
    pthread_mutex_unlock(&response_mutex);
    return NULL;
}

static void ask(const char *label, char *out, size_t cap) {
    printf("%s", label); fflush(stdout);
    if (!fgets(out, (int)cap, stdin)) { out[0] = 0; stdin_closed = 1; return; }
    out[strcspn(out, "\r\n")] = 0;
}
static int ask_until(const char *label, char *out, size_t cap, long long end_at) {
    long long left = end_at - (long long)time(NULL);
    if (left <= 0) return 0;
    printf("%s", label); fflush(stdout);
#ifdef _WIN32
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD ms = left > 86400 ? 86400000u : (DWORD)(left * 1000);
    if (WaitForSingleObject(input, ms) != WAIT_OBJECT_0) return 0;
#else
    fd_set fds; FD_ZERO(&fds); FD_SET(STDIN_FILENO, &fds);
    struct timeval timeout = {(long)left, 0};
    if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &timeout) <= 0) return 0;
#endif
    if (!fgets(out, (int)cap, stdin)) { stdin_closed = 1; return 0; }
    out[strcspn(out, "\r\n")] = 0;
    return 1;
}
static int request(const char *message) {
    pthread_mutex_lock(&response_mutex);
    awaiting_response = 1; response_ready = 0;
    pthread_mutex_unlock(&response_mutex);
    if (!send_frame(sock, message)) {
        fprintf(stderr, "Connection lost.\n"); return 0;
    }
    pthread_mutex_lock(&response_mutex);
    while (!response_ready && connection_alive) pthread_cond_wait(&response_cond, &response_mutex);
    int ok = response_ready;
    awaiting_response = 0;
    pthread_mutex_unlock(&response_mutex);
    if (!ok) fprintf(stderr, "Connection lost.\n");
    return ok;
}
static int ok(const char *prefix) {
    if (strncmp(response, prefix, strlen(prefix))) {
        printf("Server: %s\n", response); return 0;
    }
    return 1;
}
static int clean(const char *s, size_t max) {
    if (!valid_field(s, max)) { puts("Input must be nonempty and cannot contain '|'."); return 0; }
    return 1;
}
static void register_user(void) {
    char user[64], pass[160], r[20], code[80] = "", msg[400];
    ask("Username: ", user, sizeof(user));
    ask("Password: ", pass, sizeof(pass));
    ask("Role (student/admin): ", r, sizeof(r));
    if (!strcmp(r, "admin")) ask("Admin code: ", code, sizeof(code));
    if (!clean(user, 32) || !clean(pass, 128) || (strcmp(r, "student") && strcmp(r, "admin"))) return;
    snprintf(msg, sizeof(msg), "REGISTER|%s|%s|%s|%s", user, pass, r, code);
    if (request(msg)) puts(response);
}
static void login_user(void) {
    char user[64], pass[160], msg[320];
    ask("Username: ", user, sizeof(user)); ask("Password: ", pass, sizeof(pass));
    if (!clean(user, 32) || !clean(pass, 128)) return;
    snprintf(msg, sizeof(msg), "LOGIN|%s|%s", user, pass);
    if (request(msg) && ok("OK|LOGIN|")) {
        snprintf(username, sizeof(username), "%s", user);
        if (strcmp(response + 9, "admin") && strcmp(response + 9, "student")) { username[0] = 0; puts("Invalid role from server."); return; }
        strcpy(role, response + 9);
        printf("Logged in as %s (%s).\n", username, role);
    }
}
static void list_rooms(void) {
    if (!request("LIST") || !ok("ROOMS")) return;
    puts("Room | Status | Questions | Duration(s) | Owner");
    char *line = strchr(response, '\n');
    while (line) {
        ++line; char *next = strchr(line, '\n');
        if (next) *next = 0;
        if (!strncmp(line, "R|", 2)) {
            for (char *p = line + 2; *p; ++p) if (*p == '|') *p = '\t';
            puts(line + 2);
        }
        line = next;
    }
}
static void create_room(void) {
    char name[64], count[16], duration[16], msg[160];
    ask("Room name: ", name, sizeof(name));
    ask("Number of questions (1-50): ", count, sizeof(count));
    ask("Duration in seconds (10-86400): ", duration, sizeof(duration));
    if (!clean(name, 48)) return;
    snprintf(msg, sizeof(msg), "CREATE|%s|%s|%s", name, count, duration);
    if (request(msg)) puts(response);
}
static void start_room(void) {
    char name[64], msg[100]; ask("Room to start: ", name, sizeof(name));
    if (!clean(name, 48)) return;
    snprintf(msg, sizeof(msg), "START|%s", name);
    if (request(msg)) puts(response);
}
static void show_results(void) {
    char name[64], msg[100]; ask("Room name: ", name, sizeof(name));
    if (!clean(name, 48)) return;
    snprintf(msg, sizeof(msg), "RESULTS|%s", name);
    if (!request(msg) || !ok("RESULTS")) return;
    puts("User | Correct | Total | Auto submitted");
    char *line = strchr(response, '\n');
    while (line) {
        ++line; char *next = strchr(line, '\n');
        if (next) *next = 0;
        if (!strncmp(line, "R|", 2)) {
            for (char *p = line + 2; *p; ++p) if (*p == '|') *p = '\t';
            puts(line + 2);
        }
        line = next;
    }
}
typedef struct {
    char text[208], a[108], b[108], c[108], d[108];
    char selected;
} Question;
static Question questions[50];

static void print_question(int pos, const Question *q) {
    printf("\n%d. %s\n A. %s\n B. %s\n C. %s\n D. %s\n",
           pos, q->text, q->a, q->b, q->c, q->d);
    printf("Current answer: %c\n", q->selected ? q->selected : '-');
}
static int parse_question(char *line, Question *q) {
    char *v[8]; int n = 0;
    v[n++] = line;
    for (char *p = line; *p; ++p) if (*p == '|') { *p = 0; if (n == 8) return 0; v[n++] = p + 1; }
    if ((n != 7 && n != 8) || strcmp(v[0], "Q")) return 0;
    snprintf(q->text, sizeof(q->text), "%s", v[2]);
    snprintf(q->a, sizeof(q->a), "%s", v[3]);
    snprintf(q->b, sizeof(q->b), "%s", v[4]);
    snprintf(q->c, sizeof(q->c), "%s", v[5]);
    snprintf(q->d, sizeof(q->d), "%s", v[6]);
    q->selected = n == 8 && strlen(v[7]) == 1 && strchr("ABCD", v[7][0]) ? v[7][0] : 0;
    return 1;
}
static void exam_menu(const char *name, char *payload) {
    char *first = strchr(payload, '\n');
    if (first) *first = 0;
    char room[64]; long long end_at; int count;
    if (sscanf(payload, "EXAM|%63[^|]|%lld|%d", room, &end_at, &count) != 3 || count < 1 || count > 50) {
        puts("Invalid exam data."); return;
    }
    memset(questions, 0, sizeof(questions));
    int loaded = 0;
    for (char *line = first ? first + 1 : NULL; line && *line; ) {
        char *next = strchr(line, '\n');
        if (next) *next = 0;
        if (loaded < count && parse_question(line, &questions[loaded])) ++loaded;
        line = next ? next + 1 : NULL;
    }
    if (loaded != count) { puts("Incomplete exam data."); return; }
    char input[32], choice[12], msg[160];
    for (; !stdin_closed;) {
        snprintf(msg, sizeof(msg), "STATUS|%s", name);
        if (!request(msg)) return;
        if (!strncmp(response, "SCORE|", 6)) { printf("Your result: %s\n", response + 6); return; }
        if (strncmp(response, "STATUS|RUNNING|", 15)) { puts(response); return; }
        long long remaining = end_at - (long long)time(NULL);
        if (remaining < 0) remaining = 0;
        printf("\nExam %s: %d questions, %lld seconds left.\n", name, count, remaining);
        for (int i = 0; i < count; ++i)
            printf("%d. %s [%c]\n", i + 1, questions[i].text, questions[i].selected ? questions[i].selected : '-');
        if (!ask_until("Question number, 's' to submit, 'r' to refresh: ", input, sizeof(input), end_at)) {
            snprintf(msg, sizeof(msg), "STATUS|%s", name);
            if (request(msg)) printf("\nTime up. %s\n", response);
            return;
        }
        if (!strcmp(input, "r")) continue;
        if (!strcmp(input, "s")) {
            if (!ask_until("Submit now? (y/n): ", choice, sizeof(choice), end_at)) continue;
            if (strcmp(choice, "y")) continue;
            snprintf(msg, sizeof(msg), "SUBMIT|%s", name);
            if (!request(msg)) return;
            if (!strncmp(response, "SCORE|", 6)) { printf("Your result: %s\n", response + 6); return; }
            puts(response); continue;
        }
        char *end; long x = strtol(input, &end, 10);
        if (*end || x < 1 || x > count) { puts("Invalid question number."); continue; }
        print_question((int)x, &questions[x - 1]);
        if (!ask_until("Answer A/B/C/D: ", choice, sizeof(choice), end_at)) continue;
        if (strlen(choice) != 1 || !strchr("ABCDabcd", choice[0])) { puts("Invalid option."); continue; }
        char answer = (char)toupper((unsigned char)choice[0]);
        snprintf(msg, sizeof(msg), "ANSWER|%s|%ld|%c", name, x, answer);
        if (!request(msg)) return;
        if (ok("OK|ANSWER_SAVED")) questions[x - 1].selected = answer;
    }
}
static void join_room(void) {
    char name[64], msg[100]; ask("Room name: ", name, sizeof(name));
    if (!clean(name, 48)) return;
    snprintf(msg, sizeof(msg), "JOIN|%s", name);
    if (!request(msg) || !ok("OK|JOINED")) return;
    puts("Joined. Waiting for the room owner to start the exam...");
    snprintf(msg, sizeof(msg), "WAIT|%s", name);
    for (;;) {
        if (!request(msg)) return;
        if (!strcmp(response, "WAITING")) { puts("Still waiting..."); continue; }
        if (!strncmp(response, "EXAM|", 5)) { exam_menu(name, response); return; }
        if (!strncmp(response, "FINISHED|", 9)) printf("Your result: %s\n", response + 9);
        else puts(response);
        return;
    }
}
static void practice(void) {
    char count_text[16], duration[16], msg[160], input[32], choice[12];
    ask("Number of questions (1-50): ", count_text, sizeof(count_text));
    ask("Duration in seconds (10-86400): ", duration, sizeof(duration));
    snprintf(msg, sizeof(msg), "PRACTICE|%s|%s", count_text, duration);
    if (!request(msg) || !ok("PRACTICE|")) return;
    int id, count; long long end_at;
    if (sscanf(response, "PRACTICE|%d|%lld|%d", &id, &end_at, &count) != 3 || count < 1 || count > 50) return;
    for (int pos = 1; pos <= count; ++pos) {
        snprintf(msg, sizeof(msg), "PQUESTION|%d|%d", id, pos);
        if (!request(msg) || !ok("Q|")) break;
        Question q; if (!parse_question(response, &q)) { puts("Invalid question."); break; }
        print_question(pos, &q);
        printf("Time left: %lld seconds\n", end_at - (long long)time(NULL));
        if (!ask_until("Answer A/B/C/D, or Enter to skip: ", input, sizeof(input), end_at)) break;
        if (strlen(input) == 1 && strchr("ABCDabcd", input[0])) {
            snprintf(msg, sizeof(msg), "PANSWER|%d|%d|%c", id, pos, toupper((unsigned char)input[0]));
            if (!request(msg)) return;
            if (strncmp(response, "OK|", 3)) { puts(response); break; }
        }
    }
    if (!ask_until("Submit practice now? (y to submit, any key to check status): ", choice, sizeof(choice), end_at)) strcpy(choice, "y");
    snprintf(msg, sizeof(msg), "%s|%d", !strcmp(choice, "y") ? "PSUBMIT" : "PSTATUS", id);
    if (request(msg)) puts(response);
}
static void add_question(void) {
    char field[8][220], msg[2000];
    const char *labels[] = {"Question: ", "Option A: ", "Option B: ", "Option C: ", "Option D: ",
                            "Correct option A/B/C/D: ", "Topic: ", "Difficulty: "};
    for (int i = 0; i < 8; ++i) {
        ask(labels[i], field[i], sizeof(field[i]));
        if (!clean(field[i], i == 0 ? 200 : 100)) return;
    }
    for (int i = 0; i < 8; ++i) if (i == 5) field[i][0] = (char)toupper((unsigned char)field[i][0]);
    snprintf(msg, sizeof(msg), "ADDQ|%s|%s|%s|%s|%s|%s|%s|%s",
             field[0], field[1], field[2], field[3], field[4], field[5], field[6], field[7]);
    if (request(msg)) puts(response);
}
static void change_password(void) {
    char old[160], next[160], msg[400];
    ask("Old password: ", old, sizeof(old)); ask("New password: ", next, sizeof(next));
    if (!clean(old, 128) || !clean(next, 128)) return;
    snprintf(msg, sizeof(msg), "CHPASS|%s|%s", old, next);
    if (request(msg)) puts(response);
}
int main(int argc, char **argv) {
    setvbuf(stdin, NULL, _IONBF, 0);
    const char *host = argc > 1 ? argv[1] : "127.0.0.1";
    int port = argc > 2 ? atoi(argv[2]) : 9000;
    if (port < 1 || port > 65535 || !net_init()) { puts("Invalid port or network initialization failed."); return 1; }
    sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr; memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1 ||
        connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) { puts("Could not connect."); return 1; }
    if (pthread_create(&receiver_id, NULL, receiver_thread, NULL) != 0) { puts("Could not start receiver thread."); close_socket(sock); return 1; }
    char choice[20];
    for (; !stdin_closed;) {
        if (!username[0]) {
            puts("\n1 Register  2 Login  0 Exit");
            ask("> ", choice, sizeof(choice));
            if (!strcmp(choice, "1")) register_user();
            else if (!strcmp(choice, "2")) login_user();
            else if (!strcmp(choice, "0")) break;
        } else {
            printf("\n%s (%s)\n1 List rooms  2 Create room  3 Join room  4 Start room\n5 Results  6 Practice  7 Add question  8 Change password  9 Logout  0 Exit\n", username, role);
            ask("> ", choice, sizeof(choice));
            if (!strcmp(choice, "1")) list_rooms();
            else if (!strcmp(choice, "2")) create_room();
            else if (!strcmp(choice, "3")) join_room();
            else if (!strcmp(choice, "4")) start_room();
            else if (!strcmp(choice, "5")) show_results();
            else if (!strcmp(choice, "6")) practice();
            else if (!strcmp(choice, "7")) add_question();
            else if (!strcmp(choice, "8")) change_password();
            else if (!strcmp(choice, "9")) { if (request("LOGOUT")) puts(response); username[0] = role[0] = 0; }
            else if (!strcmp(choice, "0")) break;
        }
    }
    shutdown(sock, 2);
    close_socket(sock);
    pthread_join(receiver_id, NULL);
    net_cleanup(); return 0;
}
