#include "password.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif

#define ITERATIONS 200000u
#define SALT_LEN 16
#define HASH_LEN 32

static int random_bytes(unsigned char *out, int len) {
#ifdef _WIN32
    return BCryptGenRandom(NULL, out, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0;
#else
    return RAND_bytes(out, len) == 1;
#endif
}
static int derive(const char *password, const unsigned char salt[SALT_LEN],
                  unsigned iterations, unsigned char out[HASH_LEN]) {
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm = NULL;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL,
                                    BCRYPT_ALG_HANDLE_HMAC_FLAG) < 0) return 0;
    NTSTATUS status = BCryptDeriveKeyPBKDF2(algorithm, (PUCHAR)password,
        (ULONG)strlen(password), (PUCHAR)salt, SALT_LEN, iterations, out, HASH_LEN, 0);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return status >= 0;
#else
    return PKCS5_PBKDF2_HMAC(password, (int)strlen(password), salt, SALT_LEN,
                            (int)iterations, EVP_sha256(), HASH_LEN, out) == 1;
#endif
}
static void to_hex(const unsigned char *bytes, int len, char *out) {
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < len; ++i) {
        out[i * 2] = digits[bytes[i] >> 4];
        out[i * 2 + 1] = digits[bytes[i] & 15];
    }
    out[len * 2] = 0;
}
static int from_hex(const char *text, int len, unsigned char *out) {
    for (int i = 0; i < len; ++i) {
        int value = 0;
        for (int j = 0; j < 2; ++j) {
            char ch = text[i * 2 + j];
            int digit = ch >= '0' && ch <= '9' ? ch - '0' :
                        ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1;
            if (digit < 0) return 0;
            value = value * 16 + digit;
        }
        out[i] = (unsigned char)value;
    }
    return 1;
}
int password_hash(const char *password, char output[128]) {
    unsigned char salt[SALT_LEN], hash[HASH_LEN];
    char salt_hex[SALT_LEN * 2 + 1], hash_hex[HASH_LEN * 2 + 1];
    if (!random_bytes(salt, SALT_LEN) || !derive(password, salt, ITERATIONS, hash)) return 0;
    to_hex(salt, SALT_LEN, salt_hex);
    to_hex(hash, HASH_LEN, hash_hex);
    snprintf(output, 128, "pbkdf2$%u$%s$%s", ITERATIONS, salt_hex, hash_hex);
    return 1;
}
int password_verify(const char *password, const char *stored) {
    unsigned iterations;
    char salt_hex[33], hash_hex[65];
    if (sscanf(stored, "pbkdf2$%u$%32[^$]$%64s", &iterations, salt_hex, hash_hex) != 3 ||
        iterations < 100000 || iterations > 1000000 || strlen(salt_hex) != 32 || strlen(hash_hex) != 64) return 0;
    unsigned char salt[SALT_LEN], expected[HASH_LEN], actual[HASH_LEN];
    if (!from_hex(salt_hex, SALT_LEN, salt) || !from_hex(hash_hex, HASH_LEN, expected) ||
        !derive(password, salt, iterations, actual)) return 0;
    unsigned difference = 0;
    for (int i = 0; i < HASH_LEN; ++i) difference |= expected[i] ^ actual[i];
    return difference == 0;
}
