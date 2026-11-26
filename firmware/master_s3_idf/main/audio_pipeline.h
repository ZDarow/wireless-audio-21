#pragma once
#include "esp_err.h"

// Audio pipeline управление (R1: декомпозиция main.cpp)
int getStatusCallback(char* buf, size_t bufSize);
esp_err_t setVolumeCallback(int volume);
int getVolumeCallback(void);
esp_err_t setMuteCallback(bool mute);
bool getMuteCallback(void);
esp_err_t setToneCallback(float bass, float treble);
void getToneCallback(float* bass, float* treble);
esp_err_t setChannelVolumesCallback(int left, int right, int sub);
void getChannelVolumesCallback(int* left, int* right, int* sub);
esp_err_t setCrossoverHzCallback(int hz);
int getCrossoverHzCallback(void);
esp_err_t setDelayCallback(const char* channel, int delayMs);
int getDelayCallback(const char* channel);
