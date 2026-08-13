// drift_correction_test.cpp — host-тест C3.4 (дрейф-коррекция).
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "drift_correction.h"

using namespace audio21;

static void expect(int cond, const char* msg) {
    if (!cond) {
        printf("FAIL: %s\n", msg);
        exit(1);
    }
}

int main() {
    DriftCorrector dc;

    dc.reset();
    expect(dc.medianMs() == 0, "medianMs init = 0");

    int32_t suggested = dc.process(1000, 1000);
    expect(suggested == 0, "no adjust on first sample");

    for (int i = 1; i <= 8; i++) {
        dc.process((uint32_t)(1000 + i), 1000);
    }
    expect(dc.medianMs() == 5, "median after 8 samples");

    suggested = dc.process(2000, 1000);
    expect(suggested == 0, "no adjust within 10 ms threshold");

    for (int i = 0; i < 8; i++) {
        dc.process((uint32_t)(2000 + i), 1000);
    }
    expect(dc.medianMs() == 1004, "median after large offset");

    printf("drift_correction_test PASS\n");
    return 0;
}
