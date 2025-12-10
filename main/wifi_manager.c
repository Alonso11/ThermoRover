/**
 * @file wifi_manager.c
 * @brief WiFi management implementation for ESP32-P4
 */

#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "WIFI_MGR";

// ============================================================================
// PRIVATE VARIABLES
// ============================================================================

static wifi_config_params_t current_config = {0};
static wifi_status_t current_status = WIFI_STATUS_DISCONNECTED;
static esp_netif_t *netif_ap = NULL;
static esp_netif_t *netif_sta = NULL;
static wifi_event_callback_t event_callback = NULL;
static int retry_count = 0;

// ============================================================================
// PRIVATE FUNCTION PROTOTYPES
// ============================================================================

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);
static esp_err_t wifi_init_ap_mode(void);
static esp_err_t wifi_init_sta_mode(void);

// ============================================================================
// EVENT HANDLER
// ============================================================================

/**
 * @brief WiFi and IP event handler
 * 
 * Decision: Centralized event handling for all WiFi/IP events
 * Handles both AP and STA mode events
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
                ESP_LOGI(TAG, "Station "MACSTR" connected, AID=%d",
                         MAC2STR(event->mac), event->aid);
                break;
            }
            
            case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
                ESP_LOGI(TAG, "Station "MACSTR" disconnected, AID=%d",
                         MAC2STR(event->mac), event->aid);
                break;
            }
            
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi STA started, connecting...");
                current_status = WIFI_STATUS_CONNECTING;
                esp_wifi_connect();
                break;
            
            case WIFI_EVENT_STA_DISCONNECTED:
                if (retry_count < current_config.sta_max_retry) {
                    esp_wifi_connect();
                    retry_count++;
                    ESP_LOGI(TAG, "Retry connecting to AP (%d/%d)", 
                             retry_count, current_config.sta_max_retry);
                    current_status = WIFI_STATUS_CONNECTING;
                } else {
                    ESP_LOGE(TAG, "Failed to connect to AP after %d retries", retry_count);
                    current_status = WIFI_STATUS_ERROR;
                }
                
                if (event_callback) {
                    event_callback(current_status, NULL);
                }
                break;
            
            case WIFI_EVENT_AP_START:
                ESP_LOGI(TAG, "WiFi AP started");
                current_status = WIFI_STATUS_CONNECTED;
                
                esp_netif_ip_info_t ip_info;
                esp_netif_get_ip_info(netif_ap, &ip_info);
                ESP_LOGI(TAG, "AP IP Address: " IPSTR, IP2STR(&ip_info.ip));
                
                if (event_callback) {
                    event_callback(current_status, &ip_info);
                }
                break;
            
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
            current_status = WIFI_STATUS_GOT_IP;
            retry_count = 0;
            
            if (event_callback) {
                event_callback(current_status, &event->ip_info);
            }
        }
    }
}

// ============================================================================
// INITIALIZATION FUNCTIONS
// ============================================================================

static esp_err_t wifi_init_ap_mode(void)
{
    ESP_LOGI(TAG, "Initializing WiFi in AP mode");
    
    // Create AP network interface
    netif_ap = esp_netif_create_default_wifi_ap();
    if (netif_ap == NULL) {
        ESP_LOGE(TAG, "Failed to create AP network interface");
        return ESP_FAIL;
    }
    
    // Configure static IP for AP
    esp_netif_ip_info_t ip_info;
    esp_netif_dhcps_stop(netif_ap);
    
    memset(&ip_info, 0, sizeof(esp_netif_ip_info_t));
    ip_info.ip.addr = esp_ip4addr_aton(current_config.ip_addr);
    ip_info.gw.addr = esp_ip4addr_aton(current_config.gateway);
    ip_info.netmask.addr = esp_ip4addr_aton(current_config.netmask);
    
    esp_netif_set_ip_info(netif_ap, &ip_info);
    esp_netif_dhcps_start(netif_ap);
    
    // Configure WiFi AP
    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = strlen(current_config.ap_ssid),
            .channel = current_config.ap_channel,
            .max_connection = current_config.ap_max_connections,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };
    
    strcpy((char *)wifi_config.ap.ssid, current_config.ap_ssid);
    strcpy((char *)wifi_config.ap.password, current_config.ap_password);
    
    // If password is empty, use open network
    if (strlen(current_config.ap_password) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    
    ESP_LOGI(TAG, "WiFi AP configured - SSID: %s, Channel: %d", 
             current_config.ap_ssid, current_config.ap_channel);
    
    return ESP_OK;
}

static esp_err_t wifi_init_sta_mode(void)
{
    ESP_LOGI(TAG, "Initializing WiFi in STA mode");
    
    // Create STA network interface
    netif_sta = esp_netif_create_default_wifi_sta();
    if (netif_sta == NULL) {
        ESP_LOGE(TAG, "Failed to create STA network interface");
        return ESP_FAIL;
    }
    
    // Configure WiFi STA
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };
    
    strcpy((char *)wifi_config.sta.ssid, current_config.sta_ssid);
    strcpy((char *)wifi_config.sta.password, current_config.sta_password);
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    ESP_LOGI(TAG, "WiFi STA configured - SSID: %s", current_config.sta_ssid);
    
    return ESP_OK;
}

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

esp_err_t wifi_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing WiFi manager with default configuration");
    
    // Set default configuration
    current_config.mode = WIFI_MODE_AP_CUSTOM;
    strcpy(current_config.ap_ssid, WIFI_AP_SSID);
    strcpy(current_config.ap_password, WIFI_AP_PASSWORD);
    current_config.ap_channel = WIFI_AP_CHANNEL;
    current_config.ap_max_connections = WIFI_AP_MAX_CONNECTIONS;
    
    strcpy(current_config.sta_ssid, WIFI_STA_SSID);
    strcpy(current_config.sta_password, WIFI_STA_PASSWORD);
    current_config.sta_max_retry = WIFI_STA_MAX_RETRY;
    
    strcpy(current_config.ip_addr, WIFI_AP_IP_ADDR);
    strcpy(current_config.gateway, WIFI_AP_GATEWAY);
    strcpy(current_config.netmask, WIFI_AP_NETMASK);
    
    return wifi_manager_init_with_config(&current_config);
}

esp_err_t wifi_manager_init_with_config(const wifi_config_params_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid config pointer");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Initializing WiFi manager with custom configuration");
    
    // Copy configuration
    memcpy(&current_config, config, sizeof(wifi_config_params_t));
    
    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    
    // Create default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Initialize WiFi with manual config to avoid undefined CONFIG macros
    wifi_init_config_t cfg = {
        .static_rx_buf_num = 10,
        .dynamic_rx_buf_num = 32,
        .tx_buf_type = 1,
        .static_tx_buf_num = 0,
        .dynamic_tx_buf_num = 32,
        .rx_mgmt_buf_type = 1,
        .rx_mgmt_buf_num = 5,
        .cache_tx_buf_num = 0,
        .csi_enable = 0,
        .ampdu_rx_enable = 1,
        .ampdu_tx_enable = 1,
        .amsdu_tx_enable = 0,
        .nvs_enable = 1,
        .nano_enable = 0,
        .rx_ba_win = 6,
        .wifi_task_core_id = 0,
        .beacon_max_len = 752,
        .mgmt_sbuf_num = 32,
        .feature_caps = 1,
        .sta_disconnected_pm = 1,
        .espnow_max_encrypt_num = 7,
        .magic = 0x1F2F3F4F,
    };
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                &wifi_event_handler, NULL));
    
    // Initialize based on mode
    esp_err_t ret = ESP_OK;
    switch (current_config.mode) {
        case WIFI_MODE_AP_CUSTOM:
            ret = wifi_init_ap_mode();
            break;
        
        case WIFI_MODE_STA_CUSTOM:
            ret = wifi_init_sta_mode();
            break;
        
        case WIFI_MODE_APSTA_CUSTOM:
            // Initialize both AP and STA
            netif_ap = esp_netif_create_default_wifi_ap();
            netif_sta = esp_netif_create_default_wifi_sta();
            // Configure both modes...
            ESP_LOGW(TAG, "APSTA mode not fully implemented yet");
            ret = ESP_ERR_NOT_SUPPORTED;
            break;
        
        default:
            ESP_LOGE(TAG, "Invalid WiFi mode");
            ret = ESP_ERR_INVALID_ARG;
            break;
    }
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi manager initialized successfully");
    }
    
    return ret;
}

esp_err_t wifi_manager_start(void)
{
    ESP_LOGI(TAG, "Starting WiFi");
    esp_err_t ret = esp_wifi_start();
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi started successfully");
        current_status = WIFI_STATUS_CONNECTING;
    } else {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(ret));
        current_status = WIFI_STATUS_ERROR;
    }
    
    return ret;
}

esp_err_t wifi_manager_stop(void)
{
    ESP_LOGI(TAG, "Stopping WiFi");
    esp_err_t ret = esp_wifi_stop();
    
    if (ret == ESP_OK) {
        current_status = WIFI_STATUS_DISCONNECTED;
        ESP_LOGI(TAG, "WiFi stopped successfully");
    }
    
    return ret;
}

wifi_status_t wifi_manager_get_status(void)
{
    return current_status;
}

esp_err_t wifi_manager_get_ip_info(esp_netif_ip_info_t *ip_info)
{
    if (ip_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (current_config.mode == WIFI_MODE_AP_CUSTOM && netif_ap != NULL) {
        return esp_netif_get_ip_info(netif_ap, ip_info);
    } else if (current_config.mode == WIFI_MODE_STA_CUSTOM && netif_sta != NULL) {
        return esp_netif_get_ip_info(netif_sta, ip_info);
    }
    
    return ESP_FAIL;
}

esp_err_t wifi_manager_get_ssid(char *ssid)
{
    if (ssid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (current_config.mode == WIFI_MODE_AP_CUSTOM) {
        strcpy(ssid, current_config.ap_ssid);
    } else if (current_config.mode == WIFI_MODE_STA_CUSTOM) {
        strcpy(ssid, current_config.sta_ssid);
    } else {
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

int wifi_manager_get_sta_count(void)
{
    if (current_config.mode != WIFI_MODE_AP_CUSTOM) {
        return -1;
    }
    
    // Note: wifi_sta_list_t may not be available in all ESP-IDF versions
    // Return client_count if available from event tracking
    ESP_LOGW(TAG, "STA count tracking not fully implemented");
    return 0;
}

void wifi_manager_register_callback(wifi_event_callback_t callback)
{
    event_callback = callback;
    ESP_LOGI(TAG, "Event callback registered");
}

esp_err_t wifi_manager_set_mode(wifi_mode_type_t mode)
{
    current_config.mode = mode;
    ESP_LOGI(TAG, "WiFi mode set to %d (requires restart)", mode);
    return ESP_OK;
}

int wifi_manager_scan(wifi_ap_record_t *ap_info, uint16_t max_aps)
{
    if (ap_info == NULL || max_aps == 0) {
        return -1;
    }
    
    uint16_t number = max_aps;
    wifi_scan_config_t scan_config = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    
    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(ret));
        return -1;
    }
    
    ret = esp_wifi_scan_get_ap_records(&number, ap_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get scan results: %s", esp_err_to_name(ret));
        return -1;
    }
    
    ESP_LOGI(TAG, "Found %d access points", number);
    return number;
}

int8_t wifi_manager_get_rssi(void)
{
    if (current_config.mode != WIFI_MODE_STA_CUSTOM || current_status != WIFI_STATUS_GOT_IP) {
        return 0;
    }
    
    wifi_ap_record_t ap_info;
    esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
    
    if (ret == ESP_OK) {
        return ap_info.rssi;
    }
    
    return 0;
}

bool wifi_manager_is_connected(void)
{
    return (current_status == WIFI_STATUS_CONNECTED || 
            current_status == WIFI_STATUS_GOT_IP);
}