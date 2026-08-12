// auth.h — авторизация веб-интерфейса (ТЗ_Веб §18, §23).
// Header-only. Хранение пароля — SHA-256(password + salt) hex (ТЗ §22.3),
// сессия — случайный токен, CSRF — токен на POST-запросы.
#pragma once

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef ESP32
#include <esp_system.h>
#endif

namespace audio21 {

// SHA-256 (FIPS 180-4), минимальная inline-реализация без внешних библиотек.
class Sha256 {
public:
    void init() {
        h[0] = 0x6a09e667; h[1] = 0xbb67ae85; h[2] = 0x3c6ef372; h[3] = 0xa54ff53a;
        h[4] = 0x510e527f; h[5] = 0x9b05688c; h[6] = 0x1f83d9ab; h[7] = 0x5be0cd19;
        len = 0;
        buflen = 0;
    }

    void update(const void* data, size_t n) {
        const uint8_t* p = (const uint8_t*)data;
        len += n;
        while (n > 0) {
            size_t take = 64 - buflen;
            if (take > n) take = n;
            memcpy(buf + buflen, p, take);
            buflen += take;
            p += take;
            n -= take;
            if (buflen == 64) {
                transform(buf);
                buflen = 0;
            }
        }
    }

    // digest: 32 байта на выход.
    void final(uint8_t digest[32]) {
        uint64_t bits = len * 8;
        uint8_t pad = 0x80;
        update(&pad, 1);
        uint8_t zero = 0;
        while (buflen != 56) update(&zero, 1);
        uint8_t lenBytes[8];
        for (int i = 0; i < 8; i++) lenBytes[i] = (uint8_t)(bits >> (56 - i * 8));
        update(lenBytes, 8);
        for (int i = 0; i < 8; i++) {
            digest[i * 4 + 0] = (uint8_t)(h[i] >> 24);
            digest[i * 4 + 1] = (uint8_t)(h[i] >> 16);
            digest[i * 4 + 2] = (uint8_t)(h[i] >> 8);
            digest[i * 4 + 3] = (uint8_t)(h[i]);
        }
    }

    // Хелпер: hex-строка из digest (out: 65 байт).
    static void hexEncode(const uint8_t digest[32], char out[65]) {
        static const char* hex = "0123456789abcdef";
        for (int i = 0; i < 32; i++) {
            out[i * 2] = hex[digest[i] >> 4];
            out[i * 2 + 1] = hex[digest[i] & 0x0f];
        }
        out[64] = '\0';
    }

private:
    static inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void transform(const uint8_t block[64]) {
        static const uint32_t K[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
        };
        uint32_t w[64];
        for (int i = 0; i < 16; i++) {
            w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
                   ((uint32_t)block[i * 4 + 2] << 8) | (uint32_t)block[i * 4 + 3];
        }
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    uint32_t h[8];
    uint64_t len;
    size_t buflen;
    uint8_t buf[64];
};

// Вычислить SHA-256(data) в hex-строку (out: 65 байт).
inline void sha256Hex(const void* data, size_t n, char out[65]) {
    Sha256 ctx;
    ctx.init();
    ctx.update(data, n);
    uint8_t digest[32];
    ctx.final(digest);
    Sha256::hexEncode(digest, out);
}

// HMAC-SHA256 (RFC 2104): out — 32 байта.
inline void hmacSha256(const uint8_t* key, size_t keyLen,
                       const uint8_t* msg, size_t msgLen, uint8_t out[32]) {
    uint8_t k[64] = {};
    if (keyLen > 64) {
        Sha256 ctx;
        ctx.init();
        ctx.update(key, keyLen);
        ctx.final(k);  // k[0..32] = SHA-256(key)
    } else {
        memcpy(k, key, keyLen);
    }
    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; i++) {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5c;
    }
    Sha256 inner;
    inner.init();
    inner.update(ipad, 64);
    inner.update(msg, msgLen);
    uint8_t idig[32];
    inner.final(idig);
    Sha256 outer;
    outer.init();
    outer.update(opad, 64);
    outer.update(idig, 32);
    outer.final(out);
}

// PBKDF2-HMAC-SHA256 (RFC 2898), dkLen = 20 байт (один блок, 160 бит).
// out — 20 байт.
inline void pbkdf2HmacSha256(const char* password, const char* salt,
                             uint32_t iterations, uint8_t out[20]) {
    const size_t plen = strlen(password);
    const size_t slen = strlen(salt);
    uint8_t u[32];
    // U1 = HMAC(P, S || INT(1)), INT — 4-байта big-endian.
    uint8_t block[128 + 4] = {};
    memcpy(block, salt, slen);
    block[slen + 3] = 1;
    hmacSha256(reinterpret_cast<const uint8_t*>(password), plen, block, slen + 4, u);
    uint8_t t[32];
    memcpy(t, u, 32);
    for (uint32_t i = 1; i < iterations; i++) {
        hmacSha256(reinterpret_cast<const uint8_t*>(password), plen, u, 32, u);
        for (int j = 0; j < 32; j++) t[j] ^= u[j];
    }
    memcpy(out, t, 20);
}

// Hex-кодирование произвольной длины (out: 2*n+1 байт).
inline void hexEncodeN(const uint8_t* data, size_t n, char* out) {
    static const char* hex = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[i * 2] = hex[data[i] >> 4];
        out[i * 2 + 1] = hex[data[i] & 0x0f];
    }
    out[n * 2] = '\0';
}

// Авторизация: пароль, сессия, CSRF.
class Auth {
public:
    static constexpr int kTokenLen = 32;
    static constexpr int kMaxSessionAgeSec = 3600;

    // Соль приложения для PBKDF2 (см. hashPassword). Не секрет — защищает
    // от rainbow-таблиц; случайная per-device соль — техдолг T22 (NVS v5).
    static constexpr const char* kSalt = "audio21-master-salt-v1";

    // Итерации PBKDF2-HMAC-SHA256 (REPO_AUDIT V2): замедляет брутфорс в
    // 10000 раз относительно одиночного SHA-256 при том же пароле.
    static constexpr uint32_t kPbkdf2Iterations = 10000;
    // dkLen = 20 байт (160 бит) — хэш умещается в поле adminPasswordHash[65]:
    // "pbkdf2$10000$" (13) + 40 hex + null = 54. Случайная соль потребует
    // расширения поля — техдолг (T22, миграция NVS v5).
    static constexpr int kPbkdf2DkLen = 20;

    // Вычислить хэш пароля: "pbkdf2$<итерации>$<hex(PBKDF2)>". out — 65 байт.
    static void hashPassword(const char* password, char out[65]) {
        uint8_t dk[kPbkdf2DkLen];
        pbkdf2HmacSha256(password, kSalt, kPbkdf2Iterations, dk);
        char hex[kPbkdf2DkLen * 2 + 1];
        hexEncodeN(dk, kPbkdf2DkLen, hex);
        snprintf(out, 65, "pbkdf2$%u$%s", (unsigned)kPbkdf2Iterations, hex);
    }

    // Проверить пароль против сохранённого хэша. Поддерживает оба формата:
    // pbkdf2$ (текущий) и 64-hex SHA-256 (v1, обратная совместимость —
    // сохраняется до первой смены пароля, после неё пишется pbkdf2$).
    static bool checkPassword(const char* password, const char* savedHashHex) {
        if (!savedHashHex || !savedHashHex[0]) return false;
        if (strncmp(savedHashHex, "pbkdf2$", 7) == 0) {
            uint32_t iter = 0;
            const char* p = savedHashHex + 7;
            while (*p && *p != '$') {
                if (*p < '0' || *p > '9') return false;
                iter = iter * 10 + (uint32_t)(*p - '0');
                p++;
            }
            if (*p != '$' || iter == 0) return false;
            const char* hex = p + 1;
            if (strlen(hex) != (size_t)kPbkdf2DkLen * 2) return false;
            uint8_t dk[kPbkdf2DkLen];
            pbkdf2HmacSha256(password, kSalt, iter, dk);
            char computed[kPbkdf2DkLen * 2 + 1];
            hexEncodeN(dk, kPbkdf2DkLen, computed);
            return strcmp(computed, hex) == 0;
        }
        // Старый формат v1: SHA-256(kSalt + password), 64 hex.
        if (strlen(savedHashHex) != 64) return false;
        char hash[65];
        char input[128];
        int n = 0;
        size_t slen = strlen(kSalt);
        if (slen > sizeof(input) - 1) slen = sizeof(input) - 1;
        memcpy(input, kSalt, slen);
        n = (int)slen;
        size_t plen = strlen(password);
        if (plen > sizeof(input) - (size_t)n - 1) plen = sizeof(input) - (size_t)n - 1;
        memcpy(input + n, password, plen);
        sha256Hex(input, (size_t)n + plen, hash);
        return strcmp(hash, savedHashHex) == 0;
    }

    // Сессия: случайный токен (hex). Источник энтропии — аппаратный RNG.
    static void newSessionToken(char token[kTokenLen + 1]) {
#ifdef ESP32
        uint8_t bytes[kTokenLen];
        esp_fill_random(bytes, sizeof(bytes));
        for (int i = 0; i < kTokenLen; i++) {
            static const char* chars = "0123456789abcdef";
            token[i] = chars[bytes[i] & 0x0f];
        }
#else
        for (int i = 0; i < kTokenLen; i++) {
            static const char* chars = "0123456789abcdef";
            token[i] = chars[rand() & 0x0f];
        }
#endif
        token[kTokenLen] = '\0';
    }

    // CSRF-токен: SHA-256(session_token) — сервер сверяет на POST.
    static void csrfToken(const char* sessionToken, char out[65]) {
        sha256Hex(sessionToken, strlen(sessionToken), out);
    }
};

} // namespace audio21
