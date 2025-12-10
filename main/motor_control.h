/**
 * @file motor_control.h
 * @brief Motor control interface using MCPWM for L298N H-bridge driver
 * 
 * Hardware Configuration:
 * - Right motor: MCPWM0 unit (IN1=GPIO53, IN2=GPIO48)
 * - Left motor:  MCPWM1 unit (IN3=GPIO26, IN4=GPIO33)
 * - PWM frequency: 1000 Hz
 * - Resolution: 8-bit (0-255)
 */

#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include <stdint.h>
#include "esp_err.h"

// GPIO Pin Definitions for L298N Motor Driver
// Decision: Using high-speed GPIO pins suitable for PWM output
#define MOTOR_RIGHT_FORWARD_GPIO   33  // IN1 - Right motor forward
#define MOTOR_RIGHT_BACKWARD_GPIO  26  // IN2 - Right motor backward
#define MOTOR_LEFT_FORWARD_GPIO    48  // IN3 - Left motor forward
#define MOTOR_LEFT_BACKWARD_GPIO   53  // IN4 - Left motor backward

// PWM Configuration
// Decision: 1kHz is optimal for DC motors - high enough to avoid audible noise,
// low enough to minimize switching losses
#define MOTOR_PWM_FREQ_HZ         1000

// PWM Resolution
// Decision: 8-bit (255 steps) provides sufficient granularity for motor control
// while keeping calculations simple
#define MOTOR_PWM_RESOLUTION      255

/**
 * @brief Initialize MCPWM peripheral for motor control
 * 
 * Creates 4 independent PWM channels:
 * - MCPWM0: Controls right motor (forward/backward)
 * - MCPWM1: Controls left motor (forward/backward)
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t motor_control_init(void);

/**
 * @brief Set left motor speed and direction
 * 
 * @param duty Motor duty cycle
 *             Positive (1 to 255): Forward direction (IN3 PWM, IN4 LOW)
 *             Negative (-255 to -1): Backward direction (IN3 LOW, IN4 PWM)
 *             Zero (0): Brake/Coast (both pins LOW)
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t motor_set_left(int16_t duty);

/**
 * @brief Set right motor speed and direction
 * 
 * @param duty Motor duty cycle
 *             Positive (1 to 255): Forward direction (IN1 PWM, IN2 LOW)
 *             Negative (-255 to -1): Backward direction (IN1 LOW, IN2 PWM)
 *             Zero (0): Brake/Coast (both pins LOW)
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t motor_set_right(int16_t duty);

/**
 * @brief Emergency stop - immediately stop both motors
 * 
 * Sets all control pins to LOW (coast mode)
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t motor_stop(void);

/**
 * @brief Test sequence for motor validation
 * 
 * Executes a predefined sequence to verify motor connections:
 * 1. Both motors forward (1 second)
 * 2. Both motors backward (1 second)
 * 3. Left forward, right backward (1 second)
 * 4. Right forward, left backward (1 second)
 * 5. Stop
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t motor_test_sequence(void);

#endif // MOTOR_CONTROL_H