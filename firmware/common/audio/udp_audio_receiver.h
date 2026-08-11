// udp_audio_receiver.h — приёмник UDP-аудио со смартфона (спецификация §9.3).
// Header-only, без зависимостей от Arduino — тестируется на хосте.
//
// Отслеживает потери по sequence и выставляет состояние потока:
//   потеря > 50 мс   → concealment (повтор последнего фрейма с затуханием);
//   потеря > 200 мс  → ramp to mute;
//   отсутствие потока > 3 с → standby (тишина).
// PCM в буфер здесь не пишется — это задача jitter buffer; приёмник только
// определяет состояние и множитель амплитуды для маскирования потерь.
#pragma once

#include <stdint.h>

namespace audio21 {

enum class StreamState : uint8_t { Active, Conceal, RampOut, Standby };

class UdpAudioReceiver {
public:
    // Пороги §9.3.
    static constexpr uint32_t kConcealMs = 50;
    static constexpr uint32_t kRampOutMs = 200;
    static constexpr uint32_t kStandbyMs = 3000;

    // sampleRate — частота источника; msPerPacket — длительность одного пакета
    // в мс (например 5 мс при payload ≈ 960 байт: 48 кГц, стерео 16 бит).
    void configure(uint32_t sampleRate, float msPerPacket) {
        m_sampleRate = sampleRate;
        m_msPerPacket = msPerPacket;
    }

    // Обработка принятого пакета. seq — порядковый номер, ts — timestamp
    // источника (пока не используется — для синхронизации в C3.x),
    // pcm — PCM-сэмплы, n — их число, nowMs — текущее время.
    // Возвращает состояние после обработки.
    StreamState feed(uint32_t seq, uint32_t ts, const int16_t* pcm, size_t n, uint32_t nowMs) {
        m_lastNowMs = nowMs;
        m_packetsRx++;

        if (m_hasLastSeq) {
            int32_t delta = static_cast<int32_t>(seq - m_lastSeq);
            if (delta > 1) {
                // Пропуск: seq-прыжок (аккуратно к wrap 2^32).
                uint32_t lost = static_cast<uint32_t>(delta) - 1;
                m_packetsLost += lost;
                float lostMs = static_cast<float>(lost) * m_msPerPacket;
                m_state = (lostMs >= static_cast<float>(kRampOutMs))
                              ? StreamState::RampOut
                              : StreamState::Conceal;
                m_lastSeq = seq;
                // m_lastRxMs не сбрасываем: потери уже в прошлом, tick()
                // продолжит эскалацию по реальному времени.
                (void)ts; (void)pcm; (void)n;
                return m_state;
            }
            if (delta <= 0) {
                // Дубликат/переупорядочивание — состояние не трогаем.
                return m_state;
            }
            m_state = StreamState::Active;
        } else {
            m_state = StreamState::Active;
        }

        m_lastSeq = seq;
        m_lastRxMs = nowMs;
        m_hasLastSeq = true;
        (void)ts; (void)pcm; (void)n;
        return m_state;
    }

    // Периодический вызов при отсутствии пакетов (аудио-задача, каждый цикл):
    // эскалация по времени с последнего валидного пакета.
    StreamState tick(uint32_t nowMs) {
        m_lastNowMs = nowMs;
        uint32_t t = nowMs - m_lastRxMs;
        if (t >= kStandbyMs) m_state = StreamState::Standby;
        else if (t >= kRampOutMs) m_state = StreamState::RampOut;
        else if (t >= kConcealMs) m_state = StreamState::Conceal;
        return m_state;
    }

    StreamState state() const { return m_state; }

    // Множитель амплитуды для маскирования потери: 1.0 на активном потоке,
    // плавное затухание в concealment (50→200 мс), 0 при ramp-to-mute/standby.
    float concealGain() const {
        if (m_state == StreamState::Active) return 1.0f;
        if (m_state == StreamState::RampOut || m_state == StreamState::Standby)
            return 0.0f;
        uint32_t t = m_lastNowMs - m_lastRxMs;
        if (t <= kConcealMs) return 1.0f;
        if (t >= kRampOutMs) return 0.0f;
        return 1.0f - static_cast<float>(t - kConcealMs)
                    / static_cast<float>(kRampOutMs - kConcealMs);
    }

    uint32_t packetsRx() const { return m_packetsRx; }
    uint32_t packetsLost() const { return m_packetsLost; }

private:
    uint32_t m_sampleRate = 48000;
    float m_msPerPacket = 5.0f;
    StreamState m_state = StreamState::Standby;
    bool m_hasLastSeq = false;
    uint32_t m_lastSeq = 0;
    uint32_t m_lastRxMs = 0;   // время последнего валидного (непрерывного) пакета
    uint32_t m_lastNowMs = 0;  // время последнего feed/tick
    uint32_t m_packetsRx = 0;
    uint32_t m_packetsLost = 0;
};

} // namespace audio21
