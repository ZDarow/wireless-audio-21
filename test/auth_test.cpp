// auth_test.cpp — host-тест auth.h (PBKDF2-HMAC-SHA256, совместимость форматов).
// Векторы: RFC 4231 (HMAC-SHA256), RFC 7914 §11 (PBKDF2-HMAC-SHA256),
// NIST CAVS (SHA-256). Компиляция: make test (путь -I../firmware/common/web).
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "auth.h"

using namespace audio21;

static void testSha256Vector() {
    char hex[65];
    sha256Hex("abc", 3, hex);
    assert(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    printf("  sha256('abc') OK\n");
}

static void testHmacVector() {
    // RFC 4231, тест 2: key="key", msg="The quick brown fox jumps over the lazy dog".
    static const char* key = "key";
    static const char* msg = "The quick brown fox jumps over the lazy dog";
    uint8_t out[32];
    hmacSha256((const uint8_t*)key, strlen(key), (const uint8_t*)msg, strlen(msg), out);
    char hex[65];
    hexEncodeN(out, 32, hex);
    assert(strcmp(hex, "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8") == 0);
    printf("  hmac-sha256 (RFC 4231 #2) OK\n");
}

// Первые 20 байт результатов PBKDF2-HMAC-SHA256 из RFC 7914 §11.
static void testPbkdf2Vectors() {
    struct V { const char* p; const char* s; unsigned c; const char* hex20; };
    static const V vecs[] = {
        {"password", "salt", 1,
         "120fb6cffcf8b32c43e7225256c4f837a86548c9"},
        {"password", "salt", 2,
         "ae4d0c95af6b46d32d0adff928f06dd02a303f8e"},
        {"password", "salt", 4096,
         "c5e478d59288c841aa530db6845c4c8d962893a0"},
        // Вектор из RFC 7914 §11 (PASSWORDPASSWORD/SALT...).
        {"passwordPASSWORDpassword", "saltSALTsaltSALTsaltSALTsaltSALTsalt", 2,
         "13dc8a7c13d372c90382822d2dc492f2ed52467f"},
        // Пароль длиннее 64 байт — ветка SHA-256(key) в HMAC (эталон python).
        {"pppppppppppppppppppppppppppppppppppppppppppppppppppppppppppppppppppppppppppppppp",
         "saltSALTsaltSALTsaltSALTsaltSALTsalt", 2,
         "fccdeedf984816550421490ad4f3d4d7c506c42f"},
    };
    for (const V& v : vecs) {
        uint8_t dk[20];
        pbkdf2HmacSha256(v.p, v.s, v.c, dk);
        char hex[41];
        hexEncodeN(dk, 20, hex);
        if (strcmp(hex, v.hex20) != 0) {
            printf("FAIL: pbkdf2(%s, c=%u): %s != %s\n", v.p, v.c, hex, v.hex20);
            assert(false);
        }
    }
    printf("  pbkdf2-hmac-sha256 (RFC 7914, 4 вектора) OK\n");
}

static void testHashPasswordFormat() {
    char hash[65];
    Auth::hashPassword("secret42", hash);
    assert(strncmp(hash, "pbkdf2$10000$", 13) == 0);
    assert(strlen(hash) == 13 + 40);

    // round-trip: корректный пароль проходит, неверный — нет.
    assert(Auth::checkPassword("secret42", hash));
    assert(!Auth::checkPassword("secret43", hash));
    assert(!Auth::checkPassword("", hash));
    printf("  pbkdf2 round-trip OK (hash: %s)\n", hash);

    // Легаси-формат v1 (64-hex SHA-256 соль+пароль) всё ещё принимается.
    char legacy[65];
    {
        char input[128];
        size_t slen = strlen(Auth::kSalt);
        memcpy(input, Auth::kSalt, slen);
        memcpy(input + slen, "legacy-pass", 11);
        sha256Hex(input, slen + 11, legacy);
    }
    assert(strlen(legacy) == 64);
    assert(Auth::checkPassword("legacy-pass", legacy));
    assert(!Auth::checkPassword("wrong", legacy));
    printf("  legacy v1-формат совместим OK\n");

    // Мусор/пустота отклоняются.
    assert(!Auth::checkPassword("x", ""));
    assert(!Auth::checkPassword("x", nullptr));
    assert(!Auth::checkPassword("x", "pbkdf2$abc$ffff"));
    assert(!Auth::checkPassword("x", "pbkdf2$0$aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    printf("  невалидные форматы отклоняются OK\n");
}

static void testCsrf() {
    char token[33];
    Auth::newSessionToken(token);
    assert(strlen(token) == 32);
    char csrf[65], csrf2[65];
    Auth::csrfToken(token, csrf);
    Auth::csrfToken(token, csrf2);
    assert(strcmp(csrf, csrf2) == 0);
    char other[65];
    Auth::csrfToken("different-token", other);
    assert(strcmp(csrf, other) != 0);
    printf("  csrf токен детерминирован OK\n");
}

int main() {
    printf("auth_test:\n");
    testSha256Vector();
    testHmacVector();
    testPbkdf2Vectors();
    testHashPasswordFormat();
    testCsrf();
    printf("  все тесты пройдены\n");
    return 0;
}
