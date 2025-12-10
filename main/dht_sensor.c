/**
 * @file dht_sensor.c
 * @brief DHT sensor implementation using esp-idf-lib
 */

#include "dht_sensor.h"
#include <dht.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "DHT_SENSOR";

// ============================================================================
// GLOBAL STATE
// ============================================================================

static bool initialized = false;
static TaskHandle_t dht_task_handle = NULL;
static dht_reading_t latest_reading = {0};

// ============================================================================
// PRIVATE FUNCTIONS
// ============================================================================

/**
 * @brief DHT sensor reading task
 * 
 * Continuously reads temperature and humidity from the DHT sensor
 * following the library example pattern.
 */
static void dht_sensor_task(void *pvParameters)
{
    float temperature, humidity;
    
    ESP_LOGI(TAG, "DHT sensor task started on GPIO %d", DHT_GPIO_PIN);
    ESP_LOGI(TAG, "Reading interval: %d ms", DHT_READ_INTERVAL_MS);
    
    while (1) {
        // Read sensor using library function
        esp_err_t result = dht_read_float_data(
            DHT_SENSOR_TYPE,
            DHT_GPIO_PIN,
            &humidity,
            &temperature
        );
        
        if (result == ESP_OK) {
            // Update latest reading
            latest_reading.temperature = temperature;
            latest_reading.humidity = humidity;
            latest_reading.timestamp = esp_timer_get_time() / 1000; // Convert to ms
            latest_reading.valid = true;
            
            ESP_LOGI(TAG, "Temperature: %.1f°C, Humidity: %.1f%%", 
                     temperature, humidity);
        } else {
            ESP_LOGW(TAG, "Could not read data from sensor: %s", 
                     esp_err_to_name(result));
            latest_reading.valid = false;
        }
        
        // Wait before next reading (avoid sensor heating)
        // http://www.kandrsmith.org/RJS/Misc/Hygrometers/dht_sht_how_fast.html
        vTaskDelay(pdMS_TO_TICKS(DHT_READ_INTERVAL_MS));
    }
}

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

esp_err_t dht_sensor_init(void)
{
    if (initialized) {
        ESP_LOGW(TAG, "DHT sensor already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing DHT sensor on GPIO %d", DHT_GPIO_PIN);
    
    // Configure GPIO with internal pull-up
    // Following the library example pattern
    gpio_set_pull_mode(DHT_GPIO_PIN, GPIO_PULLUP_ONLY);
    
    ESP_LOGI(TAG, "GPIO %d configured with internal pull-up", DHT_GPIO_PIN);
    
    // Initialize latest reading as invalid
    latest_reading.valid = false;
    latest_reading.temperature = 0;
    latest_reading.humidity = 0;
    latest_reading.timestamp = 0;
    
    initialized = true;
    ESP_LOGI(TAG, "DHT sensor initialized successfully");
    
    return ESP_OK;
}

esp_err_t dht_sensor_start(void)
{
    if (!initialized) {
        ESP_LOGE(TAG, "DHT sensor not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (dht_task_handle != NULL) {
        ESP_LOGW(TAG, "DHT sensor task already running");
        return ESP_OK;
    }
    
    // Create task with sufficient stack size (following library example)
    BaseType_t result = xTaskCreate(
        dht_sensor_task,
        "dht_sensor",
        configMINIMAL_STACK_SIZE * 3,  // Library uses 3x minimal stack
        NULL,
        5,  // Priority 5 (same as library example)
        &dht_task_handle
    );
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create DHT sensor task");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "DHT sensor task started");
    return ESP_OK;
}

esp_err_t dht_sensor_stop(void)
{
    if (dht_task_handle == NULL) {
        ESP_LOGW(TAG, "DHT sensor task not running");
        return ESP_OK;
    }
    
    vTaskDelete(dht_task_handle);
    dht_task_handle = NULL;
    
    ESP_LOGI(TAG, "DHT sensor task stopped");
    return ESP_OK;
}

esp_err_t dht_sensor_get_reading(dht_reading_t *reading)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (reading == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *reading = latest_reading;
    return ESP_OK;
}

bool dht_sensor_is_initialized(void)
{
    return initialized;
}