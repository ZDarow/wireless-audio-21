// web_ota.cpp — OTA загрузка и проверка подписи Ed25519 (модуль A1).
// Вынесено из web_server.cpp при декомпозиции монолита.
//
// ⚠️ ОГРАНИЧЕНИЕ БЕЗОПАСНОСТИ (REPO_AUDIT V5):
// mbedTLS в ESP-IDF НЕ поддерживает Ed25519 в X.509 сертификатах
// (mbedtls#2452, mbedtls#6166, esp-idf#15336). verifyOtaSignature() использует
// raw-key API mbedtls_ed25519_read_public_key() + mbedtls_ed25519_verify()
// — это работает, но не является стандартным способом проверки подписи OTA.
// Если требуется стандартная инфраструктура PKI (X.509 сертификаты, цепочки
// доверия, отзыв сертификатов) — заменить на ECDSA-256 или RSA-3072.
//
// В текущей сборке проверка подписи заглушена (всегда возвращает true),
// так как mbedtls/ed25519.h недоступен в Arduino core 3.x.
#include "web_server.h"

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
            const char* sig = s.header("X-OTA-Signature").c_str();
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
// ВНИМАНИЕ: в текущей сборке заглушка — всегда возвращает true.
// Реальная проверка через mbedTLS требует mbedtls/ed25519.h, который
// недоступен в Arduino core 3.x (см. REPO_AUDIT V5).
bool MasterWebServer::verifyOtaSignature(const uint8_t* hash32, const char* signatureHex) {
    (void)hash32;
    (void)signatureHex;
    // Заглушка: подпись не проверяется в текущей сборке.
    // Включить проверку после добавления библиотеки Ed25519 (например, Monocypher).
    return true;
}

} // namespace audio21
