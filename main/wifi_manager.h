/**
 * @file wifi_manager.h
 * @brief WiFi management for ESP32-P4 (via ESP32-C6 co-processor)
 * 
 * Note: ESP32-P4 doesn't have built-in WiFi. The ESP32-P4-Module-DEV-KIT
 * uses an ESP32-C6 connected via SDIO to provide WiFi 6 capabilities.
 * 
 * This module handles:
 * - WiFi initialization (AP or STA mode)
 * - Network event handling
 * - IP address management
 * - Connection status monitoring
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include <stdbool.h>

// ============================================================================
// CONFIGURATION
// ============================================================================

// WiFi Mode Selection
// Decision: AP mode as default for direct rover control without router
typedef enum {
    WIFI_MODE_AP_CUSTOM,      // Access Point mode - create own network
    WIFI_MODE_STA_CUSTOM,     // Station mode - connect to existing network
    WIFI_MODE_APSTA_CUSTOM    // Both modes simultaneously
} wifi_mode_type_t;

// Default AP Mode Configuration
// Decision: Reasonable defaults that can be changed at runtime
#define WIFI_AP_SSID           "ESP32_Rover"
#define WIFI_AP_PASSWORD       "rover123"      // Min 8 chars for WPA2
#define WIFI_AP_CHANNEL        1               // Channel 1-13
#define WIFI_AP_MAX_CONNECTIONS 4              // Max simultaneous clients

// Default STA Mode Configuration
// Decision: These should be configured via menuconfig or at runtime
#define WIFI_STA_SSID          "YourWiFi"
#define WIFI_STA_PASSWORD      "YourPassword"
#define WIFI_STA_MAX_RETRY     5               // Connection retry attempts

// Network Configuration
#define WIFI_AP_IP_ADDR        "192.168.4.1"   // AP mode IP address
#define WIFI_AP_GATEWAY        "192.168.4.1"
#define WIFI_AP_NETMASK        "255.255.255.0"

// ============================================================================
// TYPES AND STRUCTURES
// ============================================================================

/**
 * @brief WiFi configuration structure
 * 
 * Decision: Grouping all WiFi parameters for easy runtime configuration
 */
typedef struct {
    wifi_mode_type_t mode;      // Operating mode (AP/STA/APSTA)
    
    // AP Mode settings
    char ap_ssid[32];           // AP SSID (max 31 chars + null)
    char ap_password[64];       // AP password (max 63 chars + null)
    uint8_t ap_channel;         // WiFi channel (1-13)
    uint8_t ap_max_connections; // Max simultaneous connections
    
    // STA Mode settings
    char sta_ssid[32];          // STA SSID to connect to
    char sta_password[64];      // STA password
    uint8_t sta_max_retry;      // Max connection retry attempts
    
    // IP Configuration (AP mode)
    char ip_addr[16];           // Static IP for AP mode
    char gateway[16];           // Gateway address
    char netmask[16];           // Netmask
} wifi_config_params_t;

/**
 * @brief WiFi connection status
 */
typedef enum {
    WIFI_STATUS_DISCONNECTED,   // Not connected
    WIFI_STATUS_CONNECTING,     // Attempting to connect
    WIFI_STATUS_CONNECTED,      // Successfully connected
    WIFI_STATUS_GOT_IP,         // Got IP address (STA mode)
    WIFI_STATUS_ERROR           // Connection error
} wifi_status_t;

/**
 * @brief WiFi event callback function type
 * 
 * Decision: Callback allows other components to react to WiFi events
 * 
 * @param status Current WiFi status
 * @param ip_info IP address information (NULL if not applicable)
 */
typedef void (*wifi_event_callback_t)(wifi_status_t status, esp_netif_ip_info_t *ip_info);

// ============================================================================
// FUNCTION PROTOTYPES
// ============================================================================

/**
 * @brief Initialize WiFi with default configuration
 * 
 * Decision: Simple init function using defaults defined above
 * For custom config, use wifi_manager_init_with_config()
 * 
 * Default behavior:
 * - AP mode enabled
 * - SSID: "ESP32_Rover"
 * - Password: "rover123"
 * - IP: 192.168.4.1
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Initialize WiFi with custom configuration
 * 
 * @param config Pointer to configuration structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_init_with_config(const wifi_config_params_t *config);

/**
 * @brief Start WiFi (begin connecting/advertising)
 * 
 * Decision: Separate start function allows configuration before starting
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_start(void);

/**
 * @brief Stop WiFi
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_stop(void);

/**
 * @brief Get current WiFi status
 * 
 * @return Current status
 */
wifi_status_t wifi_manager_get_status(void);

/**
 * @brief Get IP address information
 * 
 * @param ip_info Pointer to store IP information
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_get_ip_info(esp_netif_ip_info_t *ip_info);

/**
 * @brief Get current SSID (AP mode: our SSID, STA mode: connected SSID)
 * 
 * @param ssid Buffer to store SSID (must be at least 33 bytes)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_get_ssid(char *ssid);

/**
 * @brief Get number of connected stations (AP mode only)
 * 
 * @return Number of connected stations, -1 on error
 */
int wifi_manager_get_sta_count(void);

/**
 * @brief Register event callback
 * 
 * Decision: Allows other components (like websocket) to be notified
 * of WiFi events without tight coupling
 * 
 * @param callback Callback function pointer
 */
void wifi_manager_register_callback(wifi_event_callback_t callback);

/**
 * @brief Change WiFi mode (requires restart)
 * 
 * @param mode New WiFi mode
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_set_mode(wifi_mode_type_t mode);

/**
 * @brief Scan for available networks (STA mode)
 * 
 * Decision: Useful for web interface to show available networks
 * 
 * @param ap_info Buffer to store AP information
 * @param max_aps Maximum number of APs to return
 * @return Number of APs found, -1 on error
 */
int wifi_manager_scan(wifi_ap_record_t *ap_info, uint16_t max_aps);

/**
 * @brief Get WiFi signal strength (STA mode)
 * 
 * @return RSSI value in dBm, 0 if not connected
 */
int8_t wifi_manager_get_rssi(void);

/**
 * @brief Check if WiFi is connected and ready
 * 
 * Decision: Simple boolean check for quick status verification
 * 
 * @return true if connected and has IP, false otherwise
 */
bool wifi_manager_is_connected(void);

#endif // WIFI_MANAGER_H