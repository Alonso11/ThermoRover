/**
 * @file websocket_server.h
 * @brief WebSocket server for real-time rover control and telemetry
 * 
 * Based on ESP-IDF ws_echo_server example
 */

#ifndef WEBSOCKET_SERVER_H
#define WEBSOCKET_SERVER_H

#include "esp_err.h"
#include "esp_http_server.h"
#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// CONFIGURATION
// ============================================================================

#define WS_SERVER_PORT              80          // HTTP/WebSocket port
#define WS_MAX_CLIENTS              4           // Maximum simultaneous WebSocket clients
#define WS_KEEPALIVE_INTERVAL_SEC   30          // Ping interval
#define WS_MAX_PAYLOAD_LEN          512         // Maximum WebSocket message size

// WebSocket endpoints
#define WS_ENDPOINT                 "/ws"       // WebSocket endpoint
#define WS_ROOT_ENDPOINT            "/"         // Root HTTP endpoint (serves webpage)

// ============================================================================
// MESSAGE TYPES
// ============================================================================

typedef enum {
    WS_MSG_CONTROL,         // Joystick control command from client
    WS_MSG_TELEMETRY,       // Telemetry data to client
    WS_MSG_CONFIG,          // Configuration update
    WS_MSG_STATUS,          // Status request/response
    WS_MSG_PING,            // Keepalive ping
    WS_MSG_PONG,            // Keepalive pong
    WS_MSG_ERROR,           // Error message
    WS_MSG_UNKNOWN          // Unknown message type
} ws_message_type_t;

typedef struct {
    float angle;        // Joystick angle in radians (0 to 2π)
    float magnitude;    // Joystick magnitude (0.0 to 1.0)
    uint32_t timestamp; // Client timestamp (milliseconds)
} ws_control_msg_t;

typedef struct {
    // Motor data
    int16_t left_pwm;       // Left motor PWM (-255 to 255)
    int16_t right_pwm;      // Right motor PWM (-255 to 255)
    
    // Encoder data
    int32_t left_count;     // Left encoder count
    int32_t right_count;    // Right encoder count
    float left_rpm;         // Left motor RPM
    float right_rpm;        // Right motor RPM
    float left_distance;    // Left wheel distance (meters)
    float right_distance;   // Right wheel distance (meters)
    
    // System data
    float battery_voltage;  // Battery voltage (V)
    uint32_t uptime;        // System uptime (seconds)
    uint32_t free_heap;     // Free heap memory (bytes)
    
    // DHT sensor data
    float temperature;      // Temperature (°C) from DHT sensor
    float humidity;         // Humidity (%) from DHT sensor
    bool dht_valid;         // DHT data validity
    
    uint32_t timestamp;     // Server timestamp (milliseconds)
} ws_telemetry_msg_t;

typedef struct {
    char param_name[32];    // Parameter name
    char param_value[64];   // Parameter value
} ws_config_msg_t;

typedef struct {
    int fd;                 // Socket file descriptor
    bool connected;         // Connection status
    uint32_t last_ping;     // Last ping timestamp (ms)
} ws_client_info_t;

// ============================================================================
// CALLBACK TYPES
// ============================================================================

typedef void (*ws_control_callback_t)(const ws_control_msg_t *control);
typedef void (*ws_config_callback_t)(const ws_config_msg_t *config);

// ============================================================================
// FUNCTION PROTOTYPES
// ============================================================================

/**
 * @brief Initialize and start the WebSocket server
 * @return ESP_OK on success
 */
esp_err_t websocket_server_start(void);

/**
 * @brief Stop the WebSocket server
 * @return ESP_OK on success
 */
esp_err_t websocket_server_stop(void);

/**
 * @brief Register callback for control messages
 * @param callback Function to call when control message received
 */
void websocket_server_register_control_callback(ws_control_callback_t callback);

/**
 * @brief Register callback for configuration messages
 * @param callback Function to call when config message received
 */
void websocket_server_register_config_callback(ws_config_callback_t callback);

/**
 * @brief Broadcast telemetry data to all connected clients
 * @param telemetry Pointer to telemetry data structure
 * @return ESP_OK on success
 */
esp_err_t websocket_server_broadcast_telemetry(const ws_telemetry_msg_t *telemetry);

/**
 * @brief Send status message to specific client
 * @param fd Client socket file descriptor
 * @param status_msg Status message string (JSON)
 * @return ESP_OK on success
 */
esp_err_t websocket_server_send_status(int fd, const char *status_msg);

/**
 * @brief Get number of connected clients
 * @return Number of active WebSocket connections
 */
int websocket_server_get_client_count(void);

/**
 * @brief Get list of connected clients
 * @param clients Buffer to store client info
 * @param max_clients Maximum number of clients to return
 * @return Actual number of clients returned
 */
int websocket_server_get_clients(ws_client_info_t *clients, int max_clients);

/**
 * @brief Check if server is running
 * @return true if server is active
 */
bool websocket_server_is_running(void);

/**
 * @brief Send ping to all connected clients
 * @return ESP_OK on success
 */
esp_err_t websocket_server_ping_clients(void);

#endif // WEBSOCKET_SERVER_H