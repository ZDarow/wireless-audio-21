// util_test.cpp — host-тест утилит (timing.h, logger.h).
// Проверяет исправленные баги: Period::tick() (приоритет операторов),
// а также компиляцию/работу Logger (ранее падал на printArgs).
#include <assert.h>
#include <stdio.h>

#include "arduino_stub.h"
#include "timing.h"
#include "logger.h"

using namespace audio21;

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); g_failures++; } \
} while (0)

// --- elapsedSince: разность millis с учётом переполнения ---
static void test_elapsed_since() {
    g_millis = 100;
    CHECK(elapsedSince(10) == 90, "elapsedSince(10) при millis=100 = 90");

    // Переполнение uint32: start=0xFFFFFFF0, now=0x10 (обёртка).
    g_millis = 0x10;
    CHECK(elapsedSince(0xFFFFFFF0u) == 0x20, "elapsedSince корректна при переполнении millis");
}

// --- deadlineExpired ---
static void test_deadline() {
    g_millis = 50;
    CHECK(!deadlineExpired(100), "дедлайн 100 при now=50 не истёк");
    g_millis = 100;
    CHECK(deadlineExpired(100), "дедлайн 100 при now=100 истёк");
    g_millis = 150;
    CHECK(deadlineExpired(100), "дедлайн 100 при now=150 истёк");
}

// --- Period::tick: срабатывает ровно раз в период ---
static void test_period_tick() {
    Period p(100);
    g_millis = 0;
    CHECK(!p.tick(), "tick при старте (0 мс) не срабатывает");
    g_millis = 99;
    CHECK(!p.tick(), "tick на 99 мс не срабатывает");
    g_millis = 100;
    CHECK(p.tick(), "tick на 100 мс срабатывает");
    g_millis = 150;
    CHECK(!p.tick(), "tick на 150 мс (после срабатывания) не срабатывает");
    g_millis = 200;
    CHECK(p.tick(), "tick на 200 мс срабатывает снова");
}

// --- ExponentialSmoother ---
static void test_smoother() {
    ExponentialSmoother s(0.5f);
    CHECK(s.update(1.0f) == 1.0f, "первое значение инициализирует фильтр");
    CHECK(s.update(0.0f) == 0.5f, "второе значение сглаживается (0.5)");
    s.reset();
    CHECK(s.update(2.0f) == 2.0f, "после reset фильтр инициализируется заново");
}

// --- Logger: компилируется и не падает (уровни фильтруются) ---
static void test_logger() {
    Logger::setLevel(LogLevel::Debug);
    Logger::debug("t", "debug msg");
    Logger::info("t", "info msg");
    Logger::warn("t", "warn msg");
    Logger::error("t", "error msg");

    // Уровень Error скрывает info — вызов не должен падать.
    Logger::setLevel(LogLevel::Error);
    Logger::info("t", "hidden");
    Logger::error("t", "shown");
    Logger::setLevel(LogLevel::Info);
    CHECK(true, "logger отработал без ошибок");
}

int main() {
    printf("== util_test ==\n");
    test_elapsed_since();
    test_deadline();
    test_period_tick();
    test_smoother();
    test_logger();

    if (g_failures) {
        printf("ИТОГ: %d ПРОВАЛОВ\n", g_failures);
        return 1;
    }
    printf("ИТОГ: ВСЕ ТЕСТЫ ПРОЙДЕНЫ\n");
    return 0;
}