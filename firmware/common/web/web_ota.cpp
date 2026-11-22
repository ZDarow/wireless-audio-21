// web_ota.cpp — OTA загрузка и проверка подписи Ed25519 (модуль A1).
// Вынесено из web_server.cpp при декомпозиции монолита.
#include "web_server.h"

#ifdef ESP32
#include <mbedtls/ed25519.h>
#endif

namespace audio21 {

void MasterWebServer::handleUpdateUpload(WebServer& s) {
    HTTPUpload& up = s.upload();
    if (up.status == UPLOAD_FILE_START) {
        if (sendForcedPasswordChange(s)) {
            s.send(403, "application/json",
                   "{\"ok\":false,\"error\":\"forced_password_change\"}");
            return;
        }
        if (!csrfOk(s) || !isAuthed(s)) {
            s.send(401, "application/json", "{\"ok\":false,\"error\":\"unauthorized\"}");
            return;
        }
        // V5: если настроен открытый ключ Ed25519, требуем заголовок X-OTA-Signature.
        if (m_cfg.otaPublicKey[0] != '\0') {
            const char* sig = s.header("X-OTA-Signature");
            if (!sig || strlen(sig) == 0) {
                s.send(401, "application/json",
                       "{\"ok\":false,\"error\":\"ota_signature_required\"}");
                return;
            }
            strncpy(m_otaSignatureHex, sig, sizeof(m_otaSignatureHex) - 1);
            m_otaSignatureHex[sizeof(m_otaSignatureHex) - 1] = '\0';
        } else {
            m_otaSignatureHex[0] = '\0';
        }
        m_updateActive = false;
        m_otaHashing = false;
        memset(m_otaHash, 0, sizeof(m_otaHash));
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            s.send(500, "application/json", "{\"ok\":false,\"error\":\"update begin failed\"}");
            return;
        }
        m_updateActive = true;
        m_otaHashing = true;
        m_otaSha256Ctx.init();
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (!m_updateActive) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"unauthorized\"}"); return; }
        if (Update.write(up.buf, up.currentSize) != up.currentSize) {
            Update.abort();
            m_updateActive = false;
            s.send(500, "application/json", "{\"ok\":false,\"error\":\"write failed\"}");
        }
        // V5: накапливаем SHA-256 загружаемых данных.
        if (m_otaHashing) {
            m_otaSha256Ctx.update(up.buf, up.currentSize);
        }
    } else if (up.status == UPLOAD_FILE_END) {
        if (!m_updateActive) { s.send(401, "application/json", "{\"ok\":false,\"error\":\"unauthorized\"}"); return; }
        m_updateActive = false;
        m_otaHashing = false;

        // V5: завершаем SHA-256 и проверяем подпись Ed25519.
        if (m_cfg.otaPublicKey[0] != '\0') {
            m_otaSha256Ctx.final(m_otaHash);
            if (!verifyOtaSignature(m_otaHash, m_otaSignatureHex)) {
                Update.abort();
                s.send(401, "application/json",
                       "{\"ok\":false,\"error\":\"ota_signature_invalid\"}");
                return;
            }
        }

        if (Update.end(false)) {
            s.send(200, "application/json", "{\"ok\":true,\"status\":\"update ok, rebooting\"}");
            m_updateRebootRequested = true;
        } else {
            Update.abort();
            Update.printError(Serial);
            s.send(500, "application/json", "{\"ok\":false,\"error\":\"update failed\"}");
        }
    }
}

void MasterWebServer::handleUpdateEnd(WebServer& s) {
    s.send(200, "application/json", "{\"ok\":true}");
}

// V5: проверка Ed25519 подписи OTA-образа.
// hash32 — SHA-256 образа (32 байта), signatureHex — hex-подпись (128 символов).
// Возвращает true если подпись верна (или ключ не настроен).
bool MasterWebServer::verifyOtaSignature(const uint8_t* hash32, const char* signatureHex) {
    if (!hash32 || !signatureHex) return false;
    if (strlen(signatureHex) != 128) return false;
    if (strlen(m_cfg.otaPublicKey) != 64) return false;

    // Парсим hex-подпись (64 байта).
    uint8_t signature[64];
    for (int i = 0; i < 64; i++) {
        unsigned int v;
        if (sscanf(signatureHex + i * 2, "%2x", &v) != 1) return false;
        signature[i] = static_cast<uint8_t>(v);
    }

    // Парсим hex-открытый ключ (32 байта).
    uint8_t publicKey[32];
    for (int i = 0; i < 32; i++) {
        unsigned int v;
        if (sscanf(m_cfg.otaPublicKey + i * 2, "%2x", &v) != 1) return false;
        publicKey[i] = static_cast<uint8_t>(v);
    }

#ifdef ESP32
    mbedtls_ed25519_context ctx;
    mbedtls_ed25519_init(&ctx);
    int ret = mbedtls_ed25519_read_public_key(&ctx, publicKey, 32);
    if (ret != 0) {
        mbedtls_ed25519_free(&ctx);
        return false;
    }
    ret = mbedtls_ed25519_verify(&ctx, hash32, 32, signature);
    mbedtls_ed25519_free(&ctx);
    return ret == 0;
#else
    // Не-ESP32 (хост): веб-сервер не компилируется, заглушка.
    return true;
#endif
}

} // namespace audio21
