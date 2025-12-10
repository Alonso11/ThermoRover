/**
 * @file encoder.c
 * @brief Quadrature encoder implementation using ESP32-P4 PCNT peripheral
 */

#include "encoder.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "ENCODER";

// ============================================================================
// PRIVATE STRUCTURES
// ============================================================================

/**
 * @brief Encoder instance structure
 * 
 * Decision: Encapsulating all encoder-related data in a structure
 * makes the code more maintainable and allows easy addition of features
 */
typedef struct {
    pcnt_unit_handle_t pcnt_unit;      // PCNT unit handle
    pcnt_channel_handle_t pcnt_chan_a; // Channel A handle
    pcnt_channel_handle_t pcnt_chan_b; // Channel B handle
    int32_t last_count;                // Last count reading (for RPM calculation)
    int64_t last_time_us;              // Timestamp of last reading (microseconds)
    float rpm;                         // Current RPM
    float distance_m;                  // Total distance traveled in meters
} encoder_instance_t;

// ============================================================================
// PRIVATE VARIABLES
// ============================================================================

static encoder_instance_t left_encoder = {0};
static encoder_instance_t right_encoder = {0};

// Wheel diameter for distance calculations
// Decision: Default value for Rover 5, can be changed via encoder_set_wheel_diameter()
static float wheel_diameter_mm = 65.0f;  // Typical value, adjust for actual wheels
static float wheel_circumference_m = 0.0f;

// ============================================================================
// PRIVATE FUNCTIONS
// ============================================================================

/**
 * @brief Configure VCC supply GPIO for encoder power
 * 
 * Decision: Using GPIO as 3.3V power source for encoders
 * Rover 5 encoders are rated for 5V but work fine with 3.3V
 * Alternative: External 5V supply if 3.3V proves insufficient
 * 
 * @param gpio_num GPIO pin to configure as VCC
 * @return ESP_OK on success
 */
static esp_err_t configure_vcc_gpio(int gpio_num)
{
    // Decision: Configure as output, initially LOW
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio_num),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure VCC GPIO %d: %s", gpio_num, esp_err_to_name(ret));
        return ret;
    }
    
    // Set GPIO HIGH to provide 3.3V
    // Decision: Slight delay allows GPIO state to stabilize before encoder init
    ret = gpio_set_level(gpio_num, 1);
    vTaskDelay(pdMS_TO_TICKS(10));  // 10ms delay for power stabilization
    
    ESP_LOGI(TAG, "VCC GPIO %d configured and set HIGH (3.3V)", gpio_num);
    return ret;
}

/**
 * @brief Initialize a single encoder PCNT unit
 * 
 * Decision: Using PCNT in quadrature mode (4x decoding)
 * - Increment on rising edge of A when B is LOW
 * - Decrement on rising edge of A when B is HIGH
 * - This provides maximum resolution and direction detection
 * 
 * @param encoder Pointer to encoder instance
 * @param unit_id PCNT unit number (0-7)
 * @param gpio_a GPIO for channel A
 * @param gpio_b GPIO for channel B
 * @param name Name for logging
 * @return ESP_OK on success
 */
static esp_err_t init_encoder_pcnt(encoder_instance_t *encoder, int unit_id,
                                   int gpio_a, int gpio_b, const char *name)
{
    esp_err_t ret;
    
    // Step 1: Create PCNT unit
    // Decision: Using accumulation mode to handle counter overflow/underflow
    // This allows continuous counting beyond the 16-bit limits
    pcnt_unit_config_t unit_config = {
        .low_limit = ENCODER_PCNT_L_LIM,
        .high_limit = ENCODER_PCNT_H_LIM,
        .flags.accum_count = true,  // Enable accumulation on overflow
    };
    
    ret = pcnt_new_unit(&unit_config, &encoder->pcnt_unit);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create PCNT unit for %s: %s", name, esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "PCNT unit created for %s encoder", name);
    
    // Step 2: Configure glitch filter
    // Decision: 1µs filter removes noise without affecting legitimate signal edges
    // Rover 5 encoders at max speed (~100 RPM output) produce ~200 pulses/sec
    // Pulse width ~5ms, so 1µs filter is safe
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = ENCODER_GLITCH_FILTER_NS,
    };
    ret = pcnt_unit_set_glitch_filter(encoder->pcnt_unit, &filter_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set glitch filter for %s: %s", name, esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Glitch filter configured for %s encoder", name);
    
    // Step 3: Create channel A
    // Decision: Channel A is the edge signal, Channel B is the level/control signal
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = gpio_a,
        .level_gpio_num = gpio_b,
    };
    ret = pcnt_new_channel(encoder->pcnt_unit, &chan_a_config, &encoder->pcnt_chan_a);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create channel A for %s: %s", name, esp_err_to_name(ret));
        return ret;
    }
    
    // Step 4: Create channel B
    // Decision: Swap edge/level signals for channel B to complete quadrature decoding
    pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num = gpio_b,
        .level_gpio_num = gpio_a,
    };
    ret = pcnt_new_channel(encoder->pcnt_unit, &chan_b_config, &encoder->pcnt_chan_b);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create channel B for %s: %s", name, esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Channels A and B created for %s encoder", name);
    
    // Step 5: Configure quadrature decoding actions
    // Decision: Using standard quadrature decoding pattern (4x mode)
    // Channel A actions:
    // - When B is LOW: rising edge increments (forward rotation)
    // - When B is HIGH: rising edge decrements (backward rotation)
    ret = pcnt_channel_set_edge_action(encoder->pcnt_chan_a,
                                       PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                       PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    ret |= pcnt_channel_set_level_action(encoder->pcnt_chan_a,
                                         PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                         PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    
    // Channel B actions: Mirror of channel A for 4x decoding
    ret |= pcnt_channel_set_edge_action(encoder->pcnt_chan_b,
                                        PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                                        PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    ret |= pcnt_channel_set_level_action(encoder->pcnt_chan_b,
                                         PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                         PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set channel actions for %s: %s", name, esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Quadrature decoding configured for %s encoder", name);
    
    // Step 6: Enable and start PCNT unit
    ret = pcnt_unit_enable(encoder->pcnt_unit);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable PCNT unit for %s: %s", name, esp_err_to_name(ret));
        return ret;
    }
    
    ret = pcnt_unit_clear_count(encoder->pcnt_unit);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear count for %s: %s", name, esp_err_to_name(ret));
        return ret;
    }
    
    ret = pcnt_unit_start(encoder->pcnt_unit);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start PCNT unit for %s: %s", name, esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize tracking variables
    encoder->last_count = 0;
    encoder->last_time_us = esp_timer_get_time();
    encoder->rpm = 0.0f;
    encoder->distance_m = 0.0f;
    
    ESP_LOGI(TAG, "%s encoder initialized and started", name);
    return ESP_OK;
}

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

esp_err_t encoder_init(void)
{
    ESP_LOGI(TAG, "Initializing quadrature encoders");
    
    // Calculate wheel circumference for distance calculations
    // Decision: Pre-calculate to avoid repeated division in distance functions
    wheel_circumference_m = (M_PI * wheel_diameter_mm) / 1000.0f;
    ESP_LOGI(TAG, "Wheel diameter: %.1f mm, circumference: %.3f m", 
             wheel_diameter_mm, wheel_circumference_m);
    
    // Step 1: Configure VCC supply GPIOs
    ESP_LOGI(TAG, "Configuring encoder power supply GPIOs");
    esp_err_t ret = configure_vcc_gpio(ENCODER_LEFT_VCC_GPIO);
    if (ret != ESP_OK) {
        return ret;
    }
    
    ret = configure_vcc_gpio(ENCODER_RIGHT_VCC_GPIO);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Step 2: Initialize left encoder
    ret = init_encoder_pcnt(&left_encoder, 0, 
                           ENCODER_LEFT_A_GPIO, 
                           ENCODER_LEFT_B_GPIO, 
                           "left");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize left encoder");
        return ret;
    }
    
    // Step 3: Initialize right encoder
    ret = init_encoder_pcnt(&right_encoder, 1, 
                           ENCODER_RIGHT_A_GPIO, 
                           ENCODER_RIGHT_B_GPIO, 
                           "right");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize right encoder");
        return ret;
    }
    
    ESP_LOGI(TAG, "Both encoders initialized successfully");
    ESP_LOGI(TAG, "Encoder resolution: %.2f counts per revolution", ENCODER_CPR);
    ESP_LOGI(TAG, "Gear ratio: %.1f:1", MOTOR_GEAR_RATIO);
    
    return ESP_OK;
}

esp_err_t encoder_get_count_left(int *count)
{
    if (count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return pcnt_unit_get_count(left_encoder.pcnt_unit, count);
}

esp_err_t encoder_get_count_right(int *count)
{
    if (count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return pcnt_unit_get_count(right_encoder.pcnt_unit, count);
}

esp_err_t encoder_clear_left(void)
{
    esp_err_t ret = pcnt_unit_clear_count(left_encoder.pcnt_unit);
    if (ret == ESP_OK) {
        left_encoder.last_count = 0;
        left_encoder.distance_m = 0.0f;
    }
    return ret;
}

esp_err_t encoder_clear_right(void)
{
    esp_err_t ret = pcnt_unit_clear_count(right_encoder.pcnt_unit);
    if (ret == ESP_OK) {
        right_encoder.last_count = 0;
        right_encoder.distance_m = 0.0f;
    }
    return ret;
}

esp_err_t encoder_get_rpm_left(float *rpm)
{
    if (rpm == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *rpm = left_encoder.rpm;
    return ESP_OK;
}

esp_err_t encoder_get_rpm_right(float *rpm)
{
    if (rpm == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *rpm = right_encoder.rpm;
    return ESP_OK;
}

esp_err_t encoder_get_distance_left(float *distance)
{
    if (distance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *distance = left_encoder.distance_m;
    return ESP_OK;
}

esp_err_t encoder_get_distance_right(float *distance)
{
    if (distance == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *distance = right_encoder.distance_m;
    return ESP_OK;
}

void encoder_set_wheel_diameter(float diameter_mm)
{
    wheel_diameter_mm = diameter_mm;
    wheel_circumference_m = (M_PI * wheel_diameter_mm) / 1000.0f;
    ESP_LOGI(TAG, "Wheel diameter updated: %.1f mm, circumference: %.3f m",
             wheel_diameter_mm, wheel_circumference_m);
}

esp_err_t encoder_update(void)
{
    // Decision: Update both encoders in a single function call for synchronization
    // This ensures RPM calculations use the same time reference
    
    int64_t current_time_us = esp_timer_get_time();
    
    // Update left encoder
    int current_count_left;
    esp_err_t ret = encoder_get_count_left(&current_count_left);
    if (ret == ESP_OK) {
        // Calculate delta count and time
        int32_t delta_count = current_count_left - left_encoder.last_count;
        int64_t delta_time_us = current_time_us - left_encoder.last_time_us;
        
        if (delta_time_us > 0) {
            // Calculate RPM
            // Formula: RPM = (delta_count / CPR) * (60,000,000 µs/min / delta_time_us)
            // Decision: Calculate RPM at the output shaft (after gearbox)
            float revolutions = (float)delta_count / ENCODER_CPR;
            float time_minutes = (float)delta_time_us / 60000000.0f;
            left_encoder.rpm = revolutions / time_minutes;
            
            // Calculate distance traveled
            // Decision: Accumulate distance for odometry
            float distance_delta = revolutions * wheel_circumference_m;
            left_encoder.distance_m += distance_delta;
        }
        
        left_encoder.last_count = current_count_left;
        left_encoder.last_time_us = current_time_us;
    }
    
    // Update right encoder
    int current_count_right;
    ret = encoder_get_count_right(&current_count_right);
    if (ret == ESP_OK) {
        int32_t delta_count = current_count_right - right_encoder.last_count;
        int64_t delta_time_us = current_time_us - right_encoder.last_time_us;
        
        if (delta_time_us > 0) {
            float revolutions = (float)delta_count / ENCODER_CPR;
            float time_minutes = (float)delta_time_us / 60000000.0f;
            right_encoder.rpm = revolutions / time_minutes;
            
            float distance_delta = revolutions * wheel_circumference_m;
            right_encoder.distance_m += distance_delta;
        }
        
        right_encoder.last_count = current_count_right;
        right_encoder.last_time_us = current_time_us;
    }
    
    return ESP_OK;
}

esp_err_t encoder_pause(void)
{
    esp_err_t ret = pcnt_unit_stop(left_encoder.pcnt_unit);
    ret |= pcnt_unit_stop(right_encoder.pcnt_unit);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Encoders paused");
    }
    return ret;
}

esp_err_t encoder_resume(void)
{
    esp_err_t ret = pcnt_unit_start(left_encoder.pcnt_unit);
    ret |= pcnt_unit_start(right_encoder.pcnt_unit);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Encoders resumed");
    }
    return ret;
}