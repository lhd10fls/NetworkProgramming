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
/* Application data uses an internal field representation. Only the text
 * protocol below is sent on the socket; no length prefix or pipe is transmitted. */
typedef struct { const char *internal, *wire; } Mapping;
static const Mapping mappings[] = {
    {"REGISTER", "REGISTER"}, {"LOGIN", "LOGIN"}, {"LOGOUT", "LOGOUT"},
    {"CREATE|ROOM", "CREATE ROOM"}, {"LIST|ROOMS", "LIST ROOMS"}, {"JOIN|ROOM", "JOIN ROOM"},
    {"START|ROOM", "START ROOM"}, {"AWAIT|ROOM", "AWAIT ROOM"}, {"QUESTIONS|ROOM", "QUESTIONS ROOM"},
    {"ANSWER|ROOM", "ANSWER ROOM"}, {"SUBMIT|ROOM", "SUBMIT ROOM"}, {"STATUS|ROOM", "STATUS ROOM"},
    {"RESULTS|ROOM", "RESULTS ROOM"}, {"CREATE|PRACTICE", "CREATE PRACTICE"},
    {"QUESTIONS|PRACTICE", "QUESTIONS PRACTICE"}, {"ANSWER|PRACTICE", "ANSWER PRACTICE"},
    {"SUBMIT|PRACTICE", "SUBMIT PRACTICE"}, {"STATUS|PRACTICE", "STATUS PRACTICE"},
    {"OK|REGISTERED", "110"}, {"OK|LOGIN", "111"}, {"OK|LOGOUT", "112"},
    {"OK|ROOM|WAITING", "120"}, {"ROOMS", "121"}, {"OK|JOINED", "122"},
    {"OK|STARTED", "123"}, {"WAITING", "124"}, {"EXAM", "125"},
    {"OK|ANSWER_SAVED", "126"}, {"SCORE", "127"}, {"STATUS", "128"},
    {"RESULTS", "129"}, {"FINISHED", "130"}, {"CREATED|PRACTICE", "131"},
    {"Q", "132"}, {"SCORE|PRACTICE", "133"}, {"STATUS|PRACTICE", "134"},
    {"EVENT|AUTO_SCORE", "210"},
    {"ERR|BAD_REQUEST", "400 BAD_REQUEST"}, {"ERR|TOO_MANY_FIELDS", "400 TOO_MANY_FIELDS"},
    {"ERR|UNKNOWN_COMMAND", "400 UNKNOWN_COMMAND"},
    {"ERR|LOGIN_REQUIRED", "401 LOGIN_REQUIRED"}, {"ERR|CREDENTIALS", "401 CREDENTIALS"},
    {"ERR|FORBIDDEN", "403 FORBIDDEN"},
    {"ERR|NOT_PARTICIPANT", "403 NOT_PARTICIPANT"},
    {"ERR|ROOM_NOT_FOUND", "404 ROOM_NOT_FOUND"}, {"ERR|PRACTICE_NOT_FOUND", "404 PRACTICE_NOT_FOUND"},
    {"ERR|USER_EXISTS", "409 USER_EXISTS"}, {"ERR|ROOM_EXISTS", "409 ROOM_EXISTS"},
    {"ERR|ALREADY_LOGGED_IN", "409 ALREADY_LOGGED_IN"},
    {"ERR|ROOM_NOT_WAITING", "409 ROOM_NOT_WAITING"}, {"ERR|ROOM_FULL", "409 ROOM_FULL"},
    {"ERR|NO_PARTICIPANTS", "409 NO_PARTICIPANTS"}, {"ERR|NOT_RUNNING", "409 NOT_RUNNING"},
    {"ERR|EXAM_CLOSED", "409 EXAM_CLOSED"}, {"ERR|PRACTICE_CLOSED", "409 PRACTICE_CLOSED"},
    {"ERR|RESULT_NOT_READY", "409 RESULT_NOT_READY"}, {"ERR|ROOM_NOT_FINISHED", "409 ROOM_NOT_FINISHED"},
    {"ERR|NOT_ENOUGH_QUESTIONS", "409 NOT_ENOUGH_QUESTIONS"},
    {"ERR|DB", "500 DB"}, {"ERR|PASSWORD_HASH", "500 PASSWORD_HASH"}
};
static int matches(const char *s, const char *prefix, char delimiter) {
    size_t n = strlen(prefix);
    return !strncmp(s, prefix, n) && (!s[n] || s[n] == delimiter || (delimiter == '|' && s[n] == '\n'));
}
/* Requests and responses may share action names (STATUS, RESULTS...). The caller
 * explicitly selects direction, rather than inferring it from message text. */
static int sending_response;
void net_set_server_mode(void) { sending_response = 1; }
static int encode(const char *message, char *wire, size_t cap) {
    size_t count = sizeof(mappings)/sizeof(mappings[0]);
    size_t begin = sending_response ? 18 : 0, end = sending_response ? count : 18;
    const Mapping *m = NULL;
    for (size_t i=begin;i<end;i++)
        if (matches(message,mappings[i].internal,'|') &&
            (!m || strlen(mappings[i].internal)>strlen(m->internal))) m=&mappings[i];
    if (!m) return 0;
    size_t n=strlen(m->wire); if(n+3>=cap) return 0;
    memcpy(wire,m->wire,n);
    const char *q=message+strlen(m->internal);
    while (*q) {
        if (*q=='|') q++;
        else if (*q=='\n') {
            q++;
            if(n+4>=cap)return 0;
            memcpy(wire+n," ROW",4);n+=4;
        } else return 0;
        if(n+3>=cap) return 0;
        wire[n++]=' '; wire[n++]='"';
        while(*q && *q!='|' && *q!='\n') {
            unsigned char c=(unsigned char)*q++;
            if(n+4>=cap) return 0;
            if(c=='\n') {wire[n++]='\\';wire[n++]='n';}
            else if(c=='\\'||c=='"') {wire[n++]='\\';wire[n++]=(char)c;}
            else if(c=='\r'||c<32) return 0;
            else wire[n++]=(char)c;
        }
        wire[n++]='"';
    }
    wire[n++]='\r';wire[n++]='\n';wire[n]=0; return (int)n;
}
static int decode(char *wire,char *out,size_t cap) {
    size_t count=sizeof(mappings)/sizeof(mappings[0]);
    size_t begin=sending_response?0:18,end=sending_response?18:count;
    const Mapping *m=NULL;
    for(size_t i=begin;i<end;i++)
        if(matches(wire,mappings[i].wire,' ') && (!m||strlen(mappings[i].wire)>strlen(m->wire))) m=&mappings[i];
    if(!m) { if(cap<19)return 0; strcpy(out,"UNSUPPORTED");return 1; }
    size_t n=strlen(m->internal); if(n>=cap)return 0;memcpy(out,m->internal,n);
    char *q=wire+strlen(m->wire);
    while(*q) {
        if(*q++!=' '||!*q)return 0;
        if(n+1>=cap)return 0;
        if(!strncmp(q,"ROW ",4)) {out[n++]='\n';q+=4;}
        else out[n++]='|';
        int quoted=*q=='"';if(quoted)q++;
        int closed=!quoted;
        while(*q) {
            if(quoted&&*q=='"'){q++;closed=1;break;}
            if(!quoted&&*q==' ')break;
            unsigned char c=(unsigned char)*q++;
            if(c=='\\') {c=(unsigned char)*q++;if(c=='n')c='\n';else if(c!='\\'&&c!='"')return 0;}
            if(c=='|'||c=='\r'||(c<32&&c!='\n')||n+1>=cap)return 0;
            if(c=='\n' && n && out[n-1]=='|') n--;
            out[n++]=(char)c;
        }
        if(!closed)return 0;
    }
    out[n]=0;return 1;
}
int send_frame(socket_t s,const char *message) {
    char wire[MAX_FRAME+3];
    int n=encode(message,wire,sizeof(wire));
    return n>0 && send_all(s,(const unsigned char *)wire,(size_t)n);
}
int recv_frame(socket_t s,char *out,size_t capacity) {
    char wire[MAX_FRAME+1];size_t n=0;
    for(;;) {
        char c;if(recv(s,&c,1,0)!=1)return 0;
        if(c=='\r') {if(recv(s,&c,1,0)!=1||c!='\n')return 0;break;}
        if(c=='\n'||c==0||n>=MAX_FRAME)return 0;
        wire[n++]=c;
    }
    wire[n]=0;
    if(decode(wire,out,capacity))return 1;
    if(sending_response && capacity>=12){strcpy(out,"MALFORMED");return 1;}
    return 0;
}
int valid_field(const char *s, size_t max_len) {
    size_t n;
    if (!s || !(n = strlen(s)) || n > max_len) return 0;
    for (size_t i = 0; i < n; ++i)
        if (s[i] == '|' || s[i] == '\n' || s[i] == '\r' || (unsigned char)s[i] < 32) return 0;
    return 1;
}
