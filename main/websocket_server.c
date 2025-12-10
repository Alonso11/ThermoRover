/**
 * @file websocket_server.c
 * @brief WebSocket server implementation based on ESP-IDF ws_echo_server example
 */

#include "websocket_server.h"
#include "webpage.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "WS_SERVER";

// ============================================================================
// PRIVATE VARIABLES
// ============================================================================

static httpd_handle_t server = NULL;
static ws_control_callback_t control_callback = NULL;
static ws_config_callback_t config_callback = NULL;

// Connected clients tracking
static ws_client_info_t connected_clients[WS_MAX_CLIENTS] = {0};
static int client_count = 0;

// ============================================================================
// ASYNC SEND STRUCTURES (from ESP-IDF example)
// ============================================================================

struct async_resp_arg {
    httpd_handle_t hd;
    int fd;
    httpd_ws_frame_t ws_pkt;
    char *data;  // Dynamically allocated payload
};

// ============================================================================
// PRIVATE FUNCTION PROTOTYPES
// ============================================================================

static esp_err_t root_get_handler(httpd_req_t *req);
static esp_err_t websocket_handler(httpd_req_t *req);
static void ws_async_send(void *arg);
static esp_err_t trigger_async_send(httpd_handle_t handle, int fd, const char *data);
static void process_ws_message(httpd_req_t *req, uint8_t *payload, size_t len);
static ws_message_type_t parse_message_type(const char *json_str);
static void handle_control_message(const char *json_str);
static void handle_config_message(const char *json_str);
static int find_client_slot(int fd);
static void add_client(int fd);
static void remove_client(int fd);

// ============================================================================
// ASYNC SEND IMPLEMENTATION (from ESP-IDF example)
// ============================================================================

/**
 * @brief Async send function placed in httpd work queue
 */
static void ws_async_send(void *arg)
{
    struct async_resp_arg *resp_arg = arg;
    
    // Send the frame
    httpd_ws_send_frame_async(resp_arg->hd, resp_arg->fd, &resp_arg->ws_pkt);
    
    // Free allocated memory
    if (resp_arg->data) {
        free(resp_arg->data);
    }
    free(resp_arg);
}

/**
 * @brief Trigger async send by queuing work
 */
static esp_err_t trigger_async_send(httpd_handle_t handle, int fd, const char *data)
{
    if (handle == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    struct async_resp_arg *resp_arg = malloc(sizeof(struct async_resp_arg));
    if (resp_arg == NULL) {
        ESP_LOGE(TAG, "Failed to allocate async_resp_arg");
        return ESP_ERR_NO_MEM;
    }
    
    // Allocate and copy data
    resp_arg->data = strdup(data);
    if (resp_arg->data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate data copy");
        free(resp_arg);
        return ESP_ERR_NO_MEM;
    }
    
    resp_arg->hd = handle;
    resp_arg->fd = fd;
    
    // Setup WebSocket frame
    memset(&resp_arg->ws_pkt, 0, sizeof(httpd_ws_frame_t));
    resp_arg->ws_pkt.payload = (uint8_t *)resp_arg->data;
    resp_arg->ws_pkt.len = strlen(resp_arg->data);
    resp_arg->ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    
    // Queue the work
    esp_err_t ret = httpd_queue_work(handle, ws_async_send, resp_arg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to queue work: %s", esp_err_to_name(ret));
        free(resp_arg->data);
        free(resp_arg);
    }
    
    return ret;
}

// ============================================================================
// HTTP/WEBSOCKET HANDLERS
// ============================================================================

/**
 * @brief Root endpoint handler - serves the web interface
 */
static esp_err_t root_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Serving web interface to client");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, webpage_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief WebSocket handler - based on ESP-IDF ws_echo_server example
 */
static esp_err_t websocket_handler(httpd_req_t *req)
{
    // Check if this is the handshake (HTTP GET request)
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Handshake done, new connection opened");
        add_client(httpd_req_to_sockfd(req));
        return ESP_OK;
    }
    
    // This is a WebSocket frame
    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    
    // Step 1: Get frame length (set max_len = 0)
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }
    
    ESP_LOGD(TAG, "Frame len is %d", ws_pkt.len);
    
    if (ws_pkt.len) {
        // Step 2: Allocate buffer (ws_pkt.len + 1 for NULL termination)
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL) {
            ESP_LOGE(TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        
        // Step 3: Get actual payload (set max_len = ws_pkt.len)
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }
        
        ESP_LOGD(TAG, "Got packet with message: %s", ws_pkt.payload);
    }
    
    ESP_LOGD(TAG, "Packet type: %d", ws_pkt.type);
    
    // Handle different frame types
    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT) {
        // Process JSON message
        process_ws_message(req, ws_pkt.payload, ws_pkt.len);
    } 
    else if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        ESP_LOGI(TAG, "WebSocket client disconnected");
        remove_client(httpd_req_to_sockfd(req));
    }
    else if (ws_pkt.type == HTTPD_WS_TYPE_PING) {
        // Respond with PONG
        ESP_LOGD(TAG, "Received PING, sending PONG");
        ws_pkt.type = HTTPD_WS_TYPE_PONG;
        ret = httpd_ws_send_frame(req, &ws_pkt);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send PONG: %s", esp_err_to_name(ret));
        }
    }
    
    free(buf);
    return ret;
}

// ============================================================================
// MESSAGE PROCESSING
// ============================================================================

/**
 * @brief Process received WebSocket message
 */
static void process_ws_message(httpd_req_t *req, uint8_t *payload, size_t len)
{
    if (payload == NULL || len == 0) return;
    
    const char *json_str = (const char *)payload;
    ws_message_type_t msg_type = parse_message_type(json_str);
    
    switch (msg_type) {
        case WS_MSG_CONTROL:
            handle_control_message(json_str);
            break;
        
        case WS_MSG_CONFIG:
            handle_config_message(json_str);
            break;
        
        case WS_MSG_PING:
            // Echo back as pong
            websocket_server_send_status(httpd_req_to_sockfd(req), "{\"type\":\"pong\"}");
            break;
        
        case WS_MSG_STATUS:
            // Send status response
            websocket_server_send_status(httpd_req_to_sockfd(req), 
                                        "{\"type\":\"status\",\"state\":\"ok\"}");
            break;
        
        default:
            ESP_LOGW(TAG, "Unknown message type");
            break;
    }
}

/**
 * @brief Parse message type from JSON
 */
static ws_message_type_t parse_message_type(const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return WS_MSG_UNKNOWN;
    }
    
    cJSON *type_item = cJSON_GetObjectItem(root, "type");
    if (type_item == NULL || !cJSON_IsString(type_item)) {
        cJSON_Delete(root);
        return WS_MSG_UNKNOWN;
    }
    
    const char *type_str = type_item->valuestring;
    ws_message_type_t type = WS_MSG_UNKNOWN;
    
    if (strcmp(type_str, "control") == 0) {
        type = WS_MSG_CONTROL;
    } else if (strcmp(type_str, "config") == 0) {
        type = WS_MSG_CONFIG;
    } else if (strcmp(type_str, "ping") == 0) {
        type = WS_MSG_PING;
    } else if (strcmp(type_str, "status") == 0) {
        type = WS_MSG_STATUS;
    }
    
    cJSON_Delete(root);
    return type;
}

/**
 * @brief Handle control message
 */
static void handle_control_message(const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse control message");
        return;
    }
    
    cJSON *angle_item = cJSON_GetObjectItem(root, "angle");
    cJSON *mag_item = cJSON_GetObjectItem(root, "magnitude");
    cJSON *ts_item = cJSON_GetObjectItem(root, "timestamp");
    
    if (angle_item && mag_item && cJSON_IsNumber(angle_item) && cJSON_IsNumber(mag_item)) {
        ws_control_msg_t control;
        control.angle = (float)angle_item->valuedouble;
        control.magnitude = (float)mag_item->valuedouble;
        control.timestamp = ts_item ? (uint32_t)ts_item->valueint : 0;
        
        ESP_LOGD(TAG, "Control: angle=%.2f, mag=%.2f", control.angle, control.magnitude);
        
        // Notify via callback
        if (control_callback) {
            control_callback(&control);
        }
    } else {
        ESP_LOGE(TAG, "Invalid control message format");
    }
    
    cJSON_Delete(root);
}

/**
 * @brief Handle configuration message
 */
static void handle_config_message(const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse config message");
        return;
    }
    
    cJSON *param_item = cJSON_GetObjectItem(root, "param");
    cJSON *value_item = cJSON_GetObjectItem(root, "value");
    
    if (param_item && value_item && cJSON_IsString(param_item) && cJSON_IsString(value_item)) {
        ws_config_msg_t config;
        strncpy(config.param_name, param_item->valuestring, sizeof(config.param_name) - 1);
        strncpy(config.param_value, value_item->valuestring, sizeof(config.param_value) - 1);
        
        ESP_LOGI(TAG, "Config: %s = %s", config.param_name, config.param_value);
        
        // Notify via callback
        if (config_callback) {
            config_callback(&config);
        }
    }
    
    cJSON_Delete(root);
}

// ============================================================================
// CLIENT MANAGEMENT
// ============================================================================

static int find_client_slot(int fd)
{
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (connected_clients[i].fd == fd) {
            return i;
        }
    }
    return -1;
}

static void add_client(int fd)
{
    // Find empty slot
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (!connected_clients[i].connected) {
            connected_clients[i].fd = fd;
            connected_clients[i].connected = true;
            connected_clients[i].last_ping = esp_log_timestamp();
            client_count++;
            ESP_LOGI(TAG, "Client added (fd=%d), total clients: %d", fd, client_count);
            return;
        }
    }
    ESP_LOGW(TAG, "Maximum clients reached, cannot add fd=%d", fd);
}

static void remove_client(int fd)
{
    int idx = find_client_slot(fd);
    if (idx >= 0) {
        connected_clients[idx].connected = false;
        connected_clients[idx].fd = 0;
        client_count--;
        ESP_LOGI(TAG, "Client removed (fd=%d), total clients: %d", fd, client_count);
    }
}

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

esp_err_t websocket_server_start(void)
{
    if (server != NULL) {
        ESP_LOGW(TAG, "Server already running");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Starting WebSocket server");
    
    // HTTP server configuration
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = WS_SERVER_PORT;
    config.max_open_sockets = WS_MAX_CLIENTS + 2;  // Extra for HTTP requests
    config.lru_purge_enable = true;
    
    // Start HTTP server
    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Register root handler (serves webpage)
    httpd_uri_t root_uri = {
        .uri = WS_ROOT_ENDPOINT,
        .method = HTTP_GET,
        .handler = root_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &root_uri);
    
    // Register WebSocket handler - CRITICAL: is_websocket = true
    httpd_uri_t ws_uri = {
        .uri = WS_ENDPOINT,
        .method = HTTP_GET,
        .handler = websocket_handler,
        .user_ctx = NULL,
        .is_websocket = true,  // THIS IS ESSENTIAL!
        .handle_ws_control_frames = true  // Handle PING/PONG/CLOSE
    };
    httpd_register_uri_handler(server, &ws_uri);
    
    ESP_LOGI(TAG, "WebSocket server started on port %d", WS_SERVER_PORT);
    ESP_LOGI(TAG, "  Root endpoint: http://<IP>%s", WS_ROOT_ENDPOINT);
    ESP_LOGI(TAG, "  WebSocket endpoint: ws://<IP>%s", WS_ENDPOINT);
    
    return ESP_OK;
}

esp_err_t websocket_server_stop(void)
{
    if (server == NULL) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Stopping WebSocket server");
    esp_err_t ret = httpd_stop(server);
    server = NULL;
    client_count = 0;
    memset(connected_clients, 0, sizeof(connected_clients));
    
    return ret;
}

void websocket_server_register_control_callback(ws_control_callback_t callback)
{
    control_callback = callback;
    ESP_LOGI(TAG, "Control callback registered");
}

void websocket_server_register_config_callback(ws_config_callback_t callback)
{
    config_callback = callback;
    ESP_LOGI(TAG, "Config callback registered");
}

esp_err_t websocket_server_broadcast_telemetry(const ws_telemetry_msg_t *telemetry)
{
    if (server == NULL || telemetry == NULL || client_count == 0) {
        return ESP_OK;  // No error, just nothing to do
    }
    
    // Create JSON telemetry message
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "telemetry");
    
    // Motor data
    cJSON_AddNumberToObject(root, "left_pwm", telemetry->left_pwm);
    cJSON_AddNumberToObject(root, "right_pwm", telemetry->right_pwm);
    
    // Encoder data
    cJSON_AddNumberToObject(root, "left_count", telemetry->left_count);
    cJSON_AddNumberToObject(root, "right_count", telemetry->right_count);
    cJSON_AddNumberToObject(root, "left_rpm", telemetry->left_rpm);
    cJSON_AddNumberToObject(root, "right_rpm", telemetry->right_rpm);
    cJSON_AddNumberToObject(root, "left_distance", telemetry->left_distance);
    cJSON_AddNumberToObject(root, "right_distance", telemetry->right_distance);
    
    // System data
    cJSON_AddNumberToObject(root, "battery_voltage", telemetry->battery_voltage);
    cJSON_AddNumberToObject(root, "uptime", telemetry->uptime);
    cJSON_AddNumberToObject(root, "free_heap", telemetry->free_heap);
    
    // DHT sensor data
    cJSON_AddNumberToObject(root, "temperature", telemetry->temperature);
    cJSON_AddNumberToObject(root, "humidity", telemetry->humidity);
    cJSON_AddBoolToObject(root, "dht_valid", telemetry->dht_valid);
    
    cJSON_AddNumberToObject(root, "timestamp", telemetry->timestamp);
    
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (json_str == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON string");
        return ESP_FAIL;
    }
    
    // Broadcast to all connected clients using async send
    esp_err_t ret = ESP_OK;
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (connected_clients[i].connected) {
            esp_err_t send_ret = trigger_async_send(server, connected_clients[i].fd, json_str);
            if (send_ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to queue send to fd=%d: %s", 
                         connected_clients[i].fd, esp_err_to_name(send_ret));
                ret = send_ret;
            }
        }
    }
    
    free(json_str);
    return ret;
}

esp_err_t websocket_server_send_status(int fd, const char *status_msg)
{
    if (server == NULL || status_msg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return trigger_async_send(server, fd, status_msg);
}

int websocket_server_get_client_count(void)
{
    return client_count;
}

int websocket_server_get_clients(ws_client_info_t *clients, int max_clients)
{
    if (clients == NULL) {
        return 0;
    }
    
    int count = 0;
    for (int i = 0; i < WS_MAX_CLIENTS && count < max_clients; i++) {
        if (connected_clients[i].connected) {
            memcpy(&clients[count], &connected_clients[i], sizeof(ws_client_info_t));
            count++;
        }
    }
    
    return count;
}

bool websocket_server_is_running(void)
{
    return (server != NULL);
}

esp_err_t websocket_server_ping_clients(void)
{
    if (server == NULL || client_count == 0) {
        return ESP_OK;
    }
    
    const char *ping_msg = "{\"type\":\"ping\"}";
    
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (connected_clients[i].connected) {
            trigger_async_send(server, connected_clients[i].fd, ping_msg);
            connected_clients[i].last_ping = esp_log_timestamp();
        }
    }
    
    return ESP_OK;
}