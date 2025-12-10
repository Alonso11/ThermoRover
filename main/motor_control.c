/**
 * @file motor_control.c
 * @brief Motor control implementation using ESP32-P4 MCPWM peripheral
 */

#include "motor_control.h"
#include "driver/mcpwm_prelude.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "MOTOR_CTRL";

// MCPWM Handles
// Decision: Separate handles for each motor side for independent control
typedef struct {
    mcpwm_timer_handle_t timer;
    mcpwm_oper_handle_t operator;
    mcpwm_cmpr_handle_t comparator_fwd;
    mcpwm_cmpr_handle_t comparator_bwd;
    mcpwm_gen_handle_t generator_fwd;
    mcpwm_gen_handle_t generator_bwd;
} motor_mcpwm_t;

// Left motor uses MCPWM0, Right motor uses MCPWM1
static motor_mcpwm_t left_motor;
static motor_mcpwm_t right_motor;

/**
 * @brief Configure a single motor's MCPWM unit
 * 
 * Decision: Using independent timers for each motor allows for:
 * - Separate frequency control if needed
 * - No phase interference between motors
 * - Simplified debugging (each motor is isolated)
 * 
 * @param motor Pointer to motor structure
 * @param group_id MCPWM group ID (0 or 1)
 * @param gpio_fwd GPIO for forward direction
 * @param gpio_bwd GPIO for backward direction
 * @return ESP_OK on success
 */
static esp_err_t configure_motor_mcpwm(motor_mcpwm_t *motor, int group_id, 
                                       int gpio_fwd, int gpio_bwd)
{
    esp_err_t ret;

    // Step 1: Create MCPWM timer
    // Decision: Using up-counting mode (0 -> period) for simplicity
    // Resolution calculated to achieve 8-bit duty cycle at 1kHz frequency
    mcpwm_timer_config_t timer_config = {
        .group_id = group_id,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,  // Use APB clock (typically 80MHz)
        .resolution_hz = MOTOR_PWM_FREQ_HZ * MOTOR_PWM_RESOLUTION,  // 255kHz timer resolution
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
        .period_ticks = MOTOR_PWM_RESOLUTION,  // 255 ticks = 1kHz PWM
    };
    ret = mcpwm_new_timer(&timer_config, &motor->timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create timer for group %d: %s", group_id, esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "MCPWM timer created for group %d", group_id);

    // Step 2: Create MCPWM operator
    // Decision: One operator per motor manages both forward and backward generators
    mcpwm_operator_config_t operator_config = {
        .group_id = group_id,
    };
    ret = mcpwm_new_operator(&operator_config, &motor->operator);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create operator: %s", esp_err_to_name(ret));
        return ret;
    }

    // Step 3: Connect operator to timer
    ret = mcpwm_operator_connect_timer(motor->operator, motor->timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect operator to timer: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Operator connected to timer");

    // Step 4: Create comparators for duty cycle control
    // Decision: Separate comparators for forward and backward allow independent duty cycles
    mcpwm_comparator_config_t comparator_config = {
        .flags.update_cmp_on_tez = true,  // Update on Timer Empty (start of period)
    };
    
    ret = mcpwm_new_comparator(motor->operator, &comparator_config, &motor->comparator_fwd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create forward comparator: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = mcpwm_new_comparator(motor->operator, &comparator_config, &motor->comparator_bwd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create backward comparator: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Comparators created");

    // Step 5: Create PWM generators and connect to GPIOs
    // Decision: Separate generators for forward/backward enable true H-bridge control
    mcpwm_generator_config_t generator_config_fwd = {
        .gen_gpio_num = gpio_fwd,
    };
    ret = mcpwm_new_generator(motor->operator, &generator_config_fwd, &motor->generator_fwd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create forward generator: %s", esp_err_to_name(ret));
        return ret;
    }

    mcpwm_generator_config_t generator_config_bwd = {
        .gen_gpio_num = gpio_bwd,
    };
    ret = mcpwm_new_generator(motor->operator, &generator_config_bwd, &motor->generator_bwd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create backward generator: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Generators created on GPIO %d (fwd) and %d (bwd)", gpio_fwd, gpio_bwd);

    // Step 6: Configure PWM waveform generation
    // Decision: Active-high PWM - output HIGH when counter < compare value
    // This is the standard PWM pattern for motor control
    
    // Forward generator: HIGH at timer empty, LOW at compare match
    ret = mcpwm_generator_set_action_on_timer_event(motor->generator_fwd,
        MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, 
                                      MCPWM_TIMER_EVENT_EMPTY, 
                                      MCPWM_GEN_ACTION_HIGH));
    ret |= mcpwm_generator_set_action_on_compare_event(motor->generator_fwd,
        MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, 
                                        motor->comparator_fwd, 
                                        MCPWM_GEN_ACTION_LOW));
    
    // Backward generator: Same pattern with its own comparator
    ret |= mcpwm_generator_set_action_on_timer_event(motor->generator_bwd,
        MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, 
                                      MCPWM_TIMER_EVENT_EMPTY, 
                                      MCPWM_GEN_ACTION_HIGH));
    ret |= mcpwm_generator_set_action_on_compare_event(motor->generator_bwd,
        MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, 
                                        motor->comparator_bwd, 
                                        MCPWM_GEN_ACTION_LOW));
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set generator actions: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Generator actions configured");

    // Step 7: Enable timer
    ret = mcpwm_timer_enable(motor->timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable timer: %s", esp_err_to_name(ret));
        return ret;
    }

    // Step 8: Start timer
    ret = mcpwm_timer_start_stop(motor->timer, MCPWM_TIMER_START_NO_STOP);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start timer: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "MCPWM timer started for group %d", group_id);

    return ESP_OK;
}

esp_err_t motor_control_init(void)
{
    ESP_LOGI(TAG, "Initializing motor control system");

    // Initialize left motor on MCPWM0
    // Decision: Group 0 for left motor, using GPIOs 26 (fwd) and 33 (bwd)
    esp_err_t ret = configure_motor_mcpwm(&left_motor, 0, 
                                          MOTOR_LEFT_FORWARD_GPIO, 
                                          MOTOR_LEFT_BACKWARD_GPIO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize left motor");
        return ret;
    }

    // Initialize right motor on MCPWM1
    // Decision: Group 1 for right motor, using GPIOs 53 (fwd) and 48 (bwd)
    ret = configure_motor_mcpwm(&right_motor, 1, 
                                MOTOR_RIGHT_FORWARD_GPIO, 
                                MOTOR_RIGHT_BACKWARD_GPIO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize right motor");
        return ret;
    }

    // Initial state: Both motors stopped (duty = 0)
    motor_stop();

    ESP_LOGI(TAG, "Motor control system initialized successfully");
    return ESP_OK;
}

esp_err_t motor_set_left(int16_t duty)
{
    // Clamp duty cycle to valid range
    // Decision: Clamping prevents undefined behavior from out-of-range values
    if (duty > MOTOR_PWM_RESOLUTION) duty = MOTOR_PWM_RESOLUTION;
    if (duty < -MOTOR_PWM_RESOLUTION) duty = -MOTOR_PWM_RESOLUTION;

    esp_err_t ret;

    if (duty > 0) {
        // Forward direction: IN3 (forward) gets PWM, IN4 (backward) is LOW
        // Decision: Set backward to 0 first to prevent shoot-through current
        ret = mcpwm_comparator_set_compare_value(left_motor.comparator_bwd, 0);
        ret |= mcpwm_comparator_set_compare_value(left_motor.comparator_fwd, duty);
        ESP_LOGD(TAG, "Left motor forward: duty=%d", duty);
    } 
    else if (duty < 0) {
        // Backward direction: IN4 (backward) gets PWM, IN3 (forward) is LOW
        ret = mcpwm_comparator_set_compare_value(left_motor.comparator_fwd, 0);
        ret |= mcpwm_comparator_set_compare_value(left_motor.comparator_bwd, -duty);  // Make positive
        ESP_LOGD(TAG, "Left motor backward: duty=%d", -duty);
    } 
    else {
        // Brake/Coast: Both pins LOW
        // Decision: All LOW allows motor to coast (freewheeling)
        // Alternative would be both HIGH for active braking (shorting motor terminals)
        ret = mcpwm_comparator_set_compare_value(left_motor.comparator_fwd, 0);
        ret |= mcpwm_comparator_set_compare_value(left_motor.comparator_bwd, 0);
        ESP_LOGD(TAG, "Left motor stopped");
    }

    return ret;
}

esp_err_t motor_set_right(int16_t duty)
{
    // Clamp duty cycle to valid range
    if (duty > MOTOR_PWM_RESOLUTION) duty = MOTOR_PWM_RESOLUTION;
    if (duty < -MOTOR_PWM_RESOLUTION) duty = -MOTOR_PWM_RESOLUTION;

    esp_err_t ret;

    if (duty > 0) {
        // Forward direction: IN1 (forward) gets PWM, IN2 (backward) is LOW
        ret = mcpwm_comparator_set_compare_value(right_motor.comparator_bwd, 0);
        ret |= mcpwm_comparator_set_compare_value(right_motor.comparator_fwd, duty);
        ESP_LOGD(TAG, "Right motor forward: duty=%d", duty);
    } 
    else if (duty < 0) {
        // Backward direction: IN2 (backward) gets PWM, IN1 (forward) is LOW
        ret = mcpwm_comparator_set_compare_value(right_motor.comparator_fwd, 0);
        ret |= mcpwm_comparator_set_compare_value(right_motor.comparator_bwd, -duty);
        ESP_LOGD(TAG, "Right motor backward: duty=%d", -duty);
    } 
    else {
        // Brake/Coast: Both pins LOW
        ret = mcpwm_comparator_set_compare_value(right_motor.comparator_fwd, 0);
        ret |= mcpwm_comparator_set_compare_value(right_motor.comparator_bwd, 0);
        ESP_LOGD(TAG, "Right motor stopped");
    }

    return ret;
}

esp_err_t motor_stop(void)
{
    ESP_LOGI(TAG, "Emergency stop - stopping all motors");
    
    // Set all comparators to 0 (all outputs LOW)
    esp_err_t ret = ESP_OK;
    ret |= motor_set_left(0);
    ret |= motor_set_right(0);
    
    return ret;
}

esp_err_t motor_test_sequence(void)
{
    ESP_LOGI(TAG, "Starting motor test sequence");

    // Test 1: Both motors forward at 50% speed
    ESP_LOGI(TAG, "Test 1: Both forward (50%%)");
    motor_set_left(128);   // 50% of 255
    motor_set_right(128);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Test 2: Both motors backward at 50% speed
    ESP_LOGI(TAG, "Test 2: Both backward (50%%)");
    motor_set_left(-128);
    motor_set_right(-128);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Test 3: Left forward, Right backward (rotate right)
    ESP_LOGI(TAG, "Test 3: Rotate right (left fwd, right bwd)");
    motor_set_left(128);
    motor_set_right(-128);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Test 4: Right forward, Left backward (rotate left)
    ESP_LOGI(TAG, "Test 4: Rotate left (right fwd, left bwd)");
    motor_set_left(-128);
    motor_set_right(128);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Stop all motors
    ESP_LOGI(TAG, "Test sequence complete - stopping motors");
    motor_stop();

    return ESP_OK;
}