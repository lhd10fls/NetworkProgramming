#ifndef PASSWORD_H
#define PASSWORD_H
/* PBKDF2-HMAC-SHA256 with a fresh 128-bit salt per account. */
int password_hash(const char *password, char output[128]);
int password_verify(const char *password, const char *stored);
#endif
