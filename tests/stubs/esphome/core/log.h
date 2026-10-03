#pragma once
#include <cstdio>
#define ESP_LOG_HOST_(level, tag, ...) do { if (esphome_host_log_enabled) { std::printf("[%s][%s] ", level, tag); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
inline bool esphome_host_log_enabled = false;
#define ESP_LOGCONFIG(tag, ...) ESP_LOG_HOST_("C", tag, __VA_ARGS__)
#define ESP_LOGD(tag, ...) do {} while (0)
#define ESP_LOGV(tag, ...) do {} while (0)
#define ESP_LOGI(tag, ...) ESP_LOG_HOST_("I", tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) ESP_LOG_HOST_("W", tag, __VA_ARGS__)
