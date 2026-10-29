#pragma once

#include <stdint.h>

namespace Config {

// Copy this file to config.h, then replace the API key before building.
inline constexpr char OPENWEATHER_API_KEY[] = "your-openweathermap-api-key";
inline constexpr char WEATHER_LOCATION[] = "Austin,US";

// Fixed offset from UTC in seconds. Adjust manually when daylight saving time changes.
inline constexpr long UTC_OFFSET_SECONDS = -6L * 60L * 60L;

// Change these only if your X9C10X is wired to different ESP32-C3 pins.
inline constexpr uint8_t DIGIPOT_INC_PIN = 2;
inline constexpr uint8_t DIGIPOT_UD_PIN = 1;
inline constexpr uint8_t DIGIPOT_CS_PIN = 7;
inline constexpr uint32_t DIGIPOT_RESISTANCE_OHMS = 100000;

inline constexpr uint8_t DEAD_HOURS_START = 0;
inline constexpr uint8_t DEAD_HOURS_END = 7;

}  // namespace Config
