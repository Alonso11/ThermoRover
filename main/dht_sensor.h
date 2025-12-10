/**
 * @file dht_sensor.h
 * @brief DHT sensor interface for temperature and humidity monitoring
 * 
 * Simplified wrapper around esp-idf-lib DHT driver
 */

#ifndef DHT_SENSOR_H
#define DHT_SENSOR_H

#include "esp_err.h"
#include "driver/gpio.h"
#include <stdint.h>
#include <stdbool.h>
#include <dht.h>  // Use library directly - provides DHT_TYPE_* enums

// ============================================================================
// CONFIGURATION
// ============================================================================

#define DHT_GPIO_PIN            GPIO_NUM_4          // GPIO pin for DHT sensor
#define DHT_SENSOR_TYPE         DHT_TYPE_AM2301     // DHT22 = AM2301 in library
#define DHT_READ_INTERVAL_MS    2000                // Read every 2 seconds

// ============================================================================
// DATA STRUCTURES
// ============================================================================

typedef struct {
    float temperature;      // Temperature in °C
    float humidity;         // Relative humidity in %
    uint32_t timestamp;     // Timestamp in milliseconds
    bool valid;             // Data validity flag
} dht_reading_t;

// ============================================================================
// FUNCTION PROTOTYPES
// ============================================================================

/**
 * @brief Initialize DHT sensor (configures GPIO with pull-up)
 * @return ESP_OK on success
 */
esp_err_t dht_sensor_init(void);

/**
 * @brief Start DHT sensor reading task
 * @return ESP_OK on success
 */
esp_err_t dht_sensor_start(void);

/**
 * @brief Stop DHT sensor reading task
 * @return ESP_OK on success
 */
esp_err_t dht_sensor_stop(void);

/**
 * @brief Get latest DHT reading
 * @param reading Pointer to store reading
 * @return ESP_OK on success
 */
esp_err_t dht_sensor_get_reading(dht_reading_t *reading);

/**
 * @brief Check if DHT sensor is initialized
 * @return true if initialized
 */
bool dht_sensor_is_initialized(void);

#endif // DHT_SENSOR_H