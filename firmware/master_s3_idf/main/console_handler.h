#pragma once
#include "esp_err.h"

// Консольная обработка команд (R1: декомпозиция main.cpp)
void consoleInit(void);
void consoleTick(void);
void consolePrint(const char* s);
