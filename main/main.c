/**
 * @file main.c
 * @brief ESP32-P4 Rover Control System - Fully Integrated with WiFi/WebSocket
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "nvs_flash.h"
#include "esp_timer.h"

// Project component headers
#include "motor_control.h"
#include "encoder.h"
#include "fuzzy_control.h"
#include "wifi_manager.h"
#include "websocket_server.h"
#include "dht_sensor.h"

static const char *TAG = "MAIN";

// ============================================================================
// CONFIGURATION
// ============================================================================

// Task Configuration
#define MOTOR_TASK_STACK_SIZE     4096
#define MOTOR_TASK_PRIORITY       5
#define MOTOR_TASK_CORE           0

#define TELEMETRY_TASK_STACK_SIZE 4096
#define TELEMETRY_TASK_PRIORITY   3
#define TELEMETRY_TASK_CORE       1

// Control Loop Rates
#define MOTOR_CONTROL_RATE_HZ     50
#define TELEMETRY_RATE_HZ         10

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

static TaskHandle_t motor_task_handle = NULL;
static TaskHandle_t telemetry_task_handle = NULL;

typedef struct {
    float angle;
    float magnitude;
    uint32_t timestamp;
} joystick_cmd_t;

static QueueHandle_t joystick_queue = NULL;

// Current motor PWM values (for telemetry)
static int16_t current_left_pwm = 0;
static int16_t current_right_pwm = 0;

// ============================================================================
// CALLBACKS
// ============================================================================

/**
 * @brief WebSocket control message callback
 * 
 * Decision: Callback is called from WebSocket context, posts to queue
 * for motor control task to process (proper task separation)
 */
static void websocket_control_callback(const ws_control_msg_t *control)
{
    if (control == NULL) return;
    
    joystick_cmd_t cmd = {
        .angle = control->angle,
        .magnitude = control->magnitude,
        .timestamp = control->timestamp
    };
    
    // Send to motor control task queue (non-blocking)
    if (xQueueSend(joystick_queue, &cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Joystick queue full, command dropped");
    }
}

/**
 * @brief WebSocket configuration callback
 * 
 * Decision: Handles configuration changes from web interface
 */
static void websocket_config_callback(const ws_config_msg_t *config)
{
    if (config == NULL) return;
    
    ESP_LOGI(TAG, "Config received: %s = %s", config->param_name, config->param_value);
    
    // Handle different configuration parameters
    if (strcmp(config->param_name, "control_mode") == 0) {
        if (strcmp(config->param_value, "arcade") == 0) {
            fuzzy_control_set_mode(FUZZY_MODE_ARCADE);
        } else if (strcmp(config->param_value, "tank") == 0) {
            fuzzy_control_set_mode(FUZZY_MODE_TANK);
        } else if (strcmp(config->param_value, "car") == 0) {
            fuzzy_control_set_mode(FUZZY_MODE_CAR);
        } else if (strcmp(config->param_value, "smooth") == 0) {
            fuzzy_control_set_mode(FUZZY_MODE_SMOOTH);
        }
    } else if (strcmp(config->param_name, "preset") == 0) {
        if (strcmp(config->param_value, "gentle") == 0) {
            fuzzy_control_preset_gentle();
        } else if (strcmp(config->param_value, "normal") == 0) {
            fuzzy_control_preset_normal();
        } else if (strcmp(config->param_value, "aggressive") == 0) {
            fuzzy_control_preset_aggressive();
        } else if (strcmp(config->param_value, "precision") == 0) {
            fuzzy_control_preset_precision();
        }
    }
}

/**
 * @brief WiFi event callback
 * 
 * Decision: Start WebSocket server when WiFi is ready
 */
static void wifi_event_callback(wifi_status_t status, esp_netif_ip_info_t *ip_info)
{
    if (status == WIFI_STATUS_CONNECTED || status == WIFI_STATUS_GOT_IP) {
        ESP_LOGI(TAG, "WiFi connected, starting WebSocket server");
        websocket_server_start();
        
        if (ip_info) {
            ESP_LOGI(TAG, "===============================================");
            ESP_LOGI(TAG, "Access web interface at: http://" IPSTR, IP2STR(&ip_info->ip));
            ESP_LOGI(TAG, "===============================================");
        }
    }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

static esp_err_t init_nvs(void)
{
    ESP_LOGI(TAG, "Initializing NVS");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition was truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "NVS initialized successfully");
    }
    return ret;
}

static void print_system_info(void)
{
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "ESP32-P4 Rover Control System");
    ESP_LOGI(TAG, "===========================================");
    
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "Chip: %s, Cores: %d, Revision: %d", 
             CONFIG_IDF_TARGET, chip_info.cores, chip_info.revision);
    
    uint32_t flash_size;
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        ESP_LOGI(TAG, "Flash: %lu MB", flash_size / (1024 * 1024));
    }
    
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "IDF version: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "===========================================");
}

// ============================================================================
// FREERTOS TASKS
// ============================================================================

static void motor_control_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Motor control task started on core %d", xPortGetCoreID());
    
    joystick_cmd_t cmd;
    motor_command_t motor_cmd;
    TickType_t timeout = pdMS_TO_TICKS(100);
    
    fuzzy_control_init();
    fuzzy_control_preset_normal();
    
    while (1) {
        if (xQueueReceive(joystick_queue, &cmd, timeout) == pdTRUE) {
            fuzzy_control_process(cmd.angle, cmd.magnitude, &motor_cmd);
            motor_set_left(motor_cmd.left_duty);
            motor_set_right(motor_cmd.right_duty);
            
            // Store for telemetry
            current_left_pwm = motor_cmd.left_duty;
            current_right_pwm = motor_cmd.right_duty;
        } else {
            motor_stop();
            current_left_pwm = 0;
            current_right_pwm = 0;
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000 / MOTOR_CONTROL_RATE_HZ));
    }
}

static void telemetry_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Telemetry task started on core %d", xPortGetCoreID());
    
    while (1) {
        encoder_update();
        
        // Prepare telemetry data
        ws_telemetry_msg_t telemetry = {0};
        
        // Motor data
        telemetry.left_pwm = current_left_pwm;
        telemetry.right_pwm = current_right_pwm;
        
        // Encoder data
        int left_count, right_count;
        float left_rpm, right_rpm;
        float left_dist, right_dist;
        
        encoder_get_count_left(&left_count);
        encoder_get_count_right(&right_count);
        encoder_get_rpm_left(&left_rpm);
        encoder_get_rpm_right(&right_rpm);
        encoder_get_distance_left(&left_dist);
        encoder_get_distance_right(&right_dist);
        
        telemetry.left_count = left_count;
        telemetry.right_count = right_count;
        telemetry.left_rpm = left_rpm;
        telemetry.right_rpm = right_rpm;
        telemetry.left_distance = left_dist;
        telemetry.right_distance = right_dist;
        
        // System data
        telemetry.battery_voltage = 7.2f;  // TODO: Read from ADC
        telemetry.uptime = esp_timer_get_time() / 1000000;
        telemetry.free_heap = esp_get_free_heap_size();
        
        // DHT sensor data
        dht_reading_t dht_reading;
        if (dht_sensor_get_reading(&dht_reading) == ESP_OK && dht_reading.valid) {
            telemetry.temperature = dht_reading.temperature;
            telemetry.humidity = dht_reading.humidity;
            telemetry.dht_valid = true;
        } else {
            telemetry.temperature = 0;
            telemetry.humidity = 0;
            telemetry.dht_valid = false;
        }
        
        telemetry.timestamp = esp_timer_get_time() / 1000;
        
        // Broadcast to WebSocket clients
        websocket_server_broadcast_telemetry(&telemetry);
        
        vTaskDelay(pdMS_TO_TICKS(1000 / TELEMETRY_RATE_HZ));
    }
}

// ============================================================================
// MAIN
// ============================================================================

void app_main(void)
{
    ESP_LOGI(TAG, "Rover Control System starting...");
    
    // Phase 1: Core initialization
    ESP_ERROR_CHECK(init_nvs());
    print_system_info();
    
    // Phase 2: Hardware initialization
    ESP_LOGI(TAG, "Initializing hardware...");
    
    esp_err_t ret = motor_control_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Motor control init failed!");
        return;
    }
    
    ret = encoder_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Encoder init failed, continuing without encoders");
    }
    
    fuzzy_control_init();
    
    // Initialize DHT sensor (optional - system works without it)
    ret = dht_sensor_init();
    if (ret == ESP_OK) {
        ret = dht_sensor_start();
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "DHT sensor initialized and started");
        } else {
            ESP_LOGW(TAG, "DHT sensor start failed, continuing without sensor");
        }
    } else {
        ESP_LOGW(TAG, "DHT sensor init failed, continuing without sensor");
    }
    
    // Phase 3: Run hardware test
    ESP_LOGI(TAG, "Running hardware test...");
    motor_test_sequence();
    
    // Phase 4: Network initialization
    ESP_LOGI(TAG, "Initializing WiFi...");
    ret = wifi_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed!");
        return;
    }
    
    wifi_manager_register_callback(wifi_event_callback);
    
    ret = wifi_manager_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed!");
        return;
    }
    
    // Phase 5: WebSocket initialization
    ESP_LOGI(TAG, "Initializing WebSocket server...");
    ret = websocket_server_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WebSocket init failed!");
        return;
    }
    
    websocket_server_register_control_callback(websocket_control_callback);
    websocket_server_register_config_callback(websocket_config_callback);
    
    // Phase 6: Create tasks
    joystick_queue = xQueueCreate(10, sizeof(joystick_cmd_t));
    if (joystick_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create queue!");
        return;
    }
    
    xTaskCreatePinnedToCore(motor_control_task, "motor_ctrl",
                            MOTOR_TASK_STACK_SIZE, NULL,
                            MOTOR_TASK_PRIORITY, &motor_task_handle,
                            MOTOR_TASK_CORE);
    
    xTaskCreatePinnedToCore(telemetry_task, "telemetry",
                            TELEMETRY_TASK_STACK_SIZE, NULL,
                            TELEMETRY_TASK_PRIORITY, &telemetry_task_handle,
                            TELEMETRY_TASK_CORE);
    
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "System ready!");
    ESP_LOGI(TAG, "Connect to WiFi: %s", WIFI_AP_SSID);
    ESP_LOGI(TAG, "Password: %s", WIFI_AP_PASSWORD);
    ESP_LOGI(TAG, "Then open: http://192.168.4.1");
    ESP_LOGI(TAG, "===========================================");
    
    // Main loop: System monitoring
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "Status: Free heap=%lu, Clients=%d",
                 esp_get_free_heap_size(),
                 websocket_server_get_client_count());
    }
}