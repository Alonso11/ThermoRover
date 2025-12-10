/**
 * @file encoder.h
 * @brief Quadrature encoder interface using PCNT peripheral
 * 
 * Hardware Configuration:
 * - Left encoder:  Channel A=GPIO3, Channel B=GPIO2, VCC=GPIO0 (3.3V output)
 * - Right encoder: Channel A=GPIO36, Channel B=GPIO1, VCC=GPIO32 (3.3V output)
 * - Encoder type: Optical quadrature
 * - Resolution: 1000 pulses per 3 wheel rotations (333.33 PPR)
 * - Gear ratio: 86.8:1
 */

#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>
#include "esp_err.h"

// ============================================================================
// GPIO PIN DEFINITIONS
// ============================================================================

// Left Encoder Pins
// Decision: Using consecutive GPIOs for cleaner wiring where possible
#define ENCODER_LEFT_A_GPIO       3     // Channel A (quadrature signal)
#define ENCODER_LEFT_B_GPIO       2     // Channel B (quadrature signal)
#define ENCODER_LEFT_VCC_GPIO     0     // Power supply (3.3V output)

// Right Encoder Pins
#define ENCODER_RIGHT_A_GPIO      36    // Channel A (quadrature signal)
#define ENCODER_RIGHT_B_GPIO      1     // Channel B (quadrature signal)
#define ENCODER_RIGHT_VCC_GPIO    32    // Power supply (3.3V output)

// ============================================================================
// ENCODER SPECIFICATIONS
// ============================================================================

// Rover 5 Encoder Specifications
// Decision: Based on datasheet - 1000 pulses per 3 wheel rotations
// With quadrature decoding (4x mode), we get 4 counts per pulse
#define ENCODER_PULSES_PER_3_REVS 1000
#define ENCODER_PPR               (ENCODER_PULSES_PER_3_REVS / 3.0f)  // ~333.33
#define ENCODER_CPR               (ENCODER_PPR * 4.0f)  // Counts per revolution in 4x mode (~1333.33)

// Mechanical specifications
#define MOTOR_GEAR_RATIO          86.8f  // Gearbox reduction ratio

// Glitch Filter Configuration
// Decision: 1 microsecond filter removes electrical noise without affecting signal
#define ENCODER_GLITCH_FILTER_NS  1000   // 1 µs glitch filter

// PCNT Counter Limits
// Decision: Using full 16-bit signed range for maximum counting capability
#define ENCODER_PCNT_H_LIM        32767  // Upper limit
#define ENCODER_PCNT_L_LIM        -32768 // Lower limit

// ============================================================================
// FUNCTION PROTOTYPES
// ============================================================================

/**
 * @brief Initialize both quadrature encoders
 * 
 * This function:
 * 1. Configures VCC GPIOs to provide 3.3V power to encoders
 * 2. Creates PCNT units for left and right encoders
 * 3. Configures quadrature decoding (4x mode)
 * 4. Enables glitch filtering
 * 5. Starts counting
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t encoder_init(void);

/**
 * @brief Get current count value from left encoder
 * 
 * Decision: Returning raw count value allows flexible interpretation
 * Count increases when motor rotates forward, decreases when backward
 * 
 * @param count Pointer to store the count value
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t encoder_get_count_left(int *count);

/**
 * @brief Get current count value from right encoder
 * 
 * @param count Pointer to store the count value
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t encoder_get_count_right(int *count);

/**
 * @brief Clear/reset left encoder count to zero
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t encoder_clear_left(void);

/**
 * @brief Clear/reset right encoder count to zero
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t encoder_clear_right(void);

/**
 * @brief Get left motor RPM (revolutions per minute)
 * 
 * Decision: Calculating RPM at the motor shaft (after gearbox)
 * This represents actual wheel speed
 * 
 * Note: Requires periodic calling to maintain accuracy
 * Recommended call rate: 10-20 Hz
 * 
 * @param rpm Pointer to store the RPM value
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t encoder_get_rpm_left(float *rpm);

/**
 * @brief Get right motor RPM (revolutions per minute)
 * 
 * @param rpm Pointer to store the RPM value
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t encoder_get_rpm_right(float *rpm);

/**
 * @brief Get left motor distance traveled in meters
 * 
 * Decision: Useful for odometry and navigation
 * Requires wheel diameter to be set (see encoder_set_wheel_diameter)
 * 
 * @param distance Pointer to store distance in meters
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t encoder_get_distance_left(float *distance);

/**
 * @brief Get right motor distance traveled in meters
 * 
 * @param distance Pointer to store distance in meters
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t encoder_get_distance_right(float *distance);

/**
 * @brief Set wheel diameter for distance calculations
 * 
 * Decision: Making this configurable for different wheel sizes
 * Default is for Rover 5 chassis
 * 
 * @param diameter_mm Wheel diameter in millimeters
 */
void encoder_set_wheel_diameter(float diameter_mm);

/**
 * @brief Update encoder RPM calculations (call periodically)
 * 
 * Decision: Separate update function allows control over calculation timing
 * Should be called at regular intervals (e.g., 10 Hz) for accurate RPM
 * 
 * This function:
 * - Reads current encoder counts
 * - Calculates delta from last reading
 * - Computes RPM based on elapsed time
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t encoder_update(void);

/**
 * @brief Pause encoder counting (for power saving or maintenance)
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t encoder_pause(void);

/**
 * @brief Resume encoder counting after pause
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t encoder_resume(void);

#endif // ENCODER_H