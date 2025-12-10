/**
 * @file fuzzy_control.c
 * @brief Fuzzy logic control implementation for differential drive
 */

#include "fuzzy_control.h"
#include <math.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "FUZZY_CTRL";

// Current configuration
static fuzzy_config_t current_config = {0};

// Helper function prototypes
static float apply_dead_zone(float magnitude, float dead_zone);
static float apply_curve(float magnitude, fuzzy_curve_type_t curve);
static float normalize_angle(float angle);
static float clamp(float value, float min, float max);
static int16_t apply_min_duty(int16_t duty, int16_t min_duty);
static void process_arcade_mode(float angle, float magnitude, const fuzzy_config_t *config, motor_command_t *output);
static void process_tank_mode(float angle, float magnitude, const fuzzy_config_t *config, motor_command_t *output);
static void process_car_mode(float angle, float magnitude, const fuzzy_config_t *config, motor_command_t *output);
static void process_smooth_mode(float angle, float magnitude, const fuzzy_config_t *config, motor_command_t *output);

// ============================================================================
// PRIVATE HELPER FUNCTIONS
// ============================================================================

static float apply_dead_zone(float magnitude, float dead_zone)
{
    if (magnitude < dead_zone) {
        return 0.0f;
    }
    return (magnitude - dead_zone) / (1.0f - dead_zone);
}

static float apply_curve(float magnitude, fuzzy_curve_type_t curve)
{
    switch (curve) {
        case FUZZY_CURVE_LINEAR:
            return magnitude;
        case FUZZY_CURVE_QUADRATIC:
            return magnitude * magnitude;
        case FUZZY_CURVE_CUBIC:
            return magnitude * magnitude * magnitude;
        case FUZZY_CURVE_SQRT:
            return sqrtf(magnitude);
        default:
            return magnitude;
    }
}

static float normalize_angle(float angle)
{
    while (angle < 0) angle += 2.0f * M_PI;
    while (angle >= 2.0f * M_PI) angle -= 2.0f * M_PI;
    return angle;
}

static float clamp(float value, float min, float max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static int16_t apply_min_duty(int16_t duty, int16_t min_duty)
{
    if (duty == 0) {
        return 0;
    } else if (duty > 0 && duty < min_duty) {
        return min_duty;
    } else if (duty < 0 && duty > -min_duty) {
        return -min_duty;
    }
    return duty;
}

static void process_arcade_mode(float angle, float magnitude, const fuzzy_config_t *config, motor_command_t *output)
{
    float x = magnitude * cosf(angle);
    float y = magnitude * sinf(angle);
    float base_speed = y;
    float turn_rate = x * config->turn_factor;
    float left_speed = clamp(base_speed + turn_rate, -1.0f, 1.0f);
    float right_speed = clamp(base_speed - turn_rate, -1.0f, 1.0f);
    output->left_duty = (int16_t)(left_speed * config->max_duty);
    output->right_duty = (int16_t)(right_speed * config->max_duty);
}

static void process_tank_mode(float angle, float magnitude, const fuzzy_config_t *config, motor_command_t *output)
{
    float x = magnitude * cosf(angle);
    float y = magnitude * sinf(angle);
    float left_speed = clamp(y + x, -1.0f, 1.0f);
    float right_speed = clamp(y - x, -1.0f, 1.0f);
    output->left_duty = (int16_t)(left_speed * config->max_duty);
    output->right_duty = (int16_t)(right_speed * config->max_duty);
}

static void process_car_mode(float angle, float magnitude, const fuzzy_config_t *config, motor_command_t *output)
{
    angle = normalize_angle(angle);
    float forward_component = sinf(angle);
    float turn_component = fabsf(cosf(angle));
    float base_speed = forward_component;
    float reduction = turn_component * config->turn_factor;
    float left_speed, right_speed;
    if (cosf(angle) > 0) {
        left_speed = base_speed;
        right_speed = base_speed * (1.0f - reduction);
    } else {
        left_speed = base_speed * (1.0f - reduction);
        right_speed = base_speed;
    }
    left_speed *= magnitude;
    right_speed *= magnitude;
    output->left_duty = (int16_t)(left_speed * config->max_duty);
    output->right_duty = (int16_t)(right_speed * config->max_duty);
}

static void process_smooth_mode(float angle, float magnitude, const fuzzy_config_t *config, motor_command_t *output)
{
    float smooth_turn_factor = config->turn_factor * 0.7f;
    float x = magnitude * cosf(angle);
    float y = magnitude * sinf(angle);
    float base_speed = y;
    float turn_rate = x * smooth_turn_factor;
    float left_speed = clamp(base_speed + turn_rate, -1.0f, 1.0f);
    float right_speed = clamp(base_speed - turn_rate, -1.0f, 1.0f);
    output->left_duty = (int16_t)(left_speed * config->max_duty);
    output->right_duty = (int16_t)(right_speed * config->max_duty);
}

// ============================================================================
// PUBLIC FUNCTIONS
// ============================================================================

void fuzzy_control_init(void)
{
    ESP_LOGI(TAG, "Initializing fuzzy control system");
    current_config.mode = FUZZY_MODE_ARCADE;
    current_config.curve = FUZZY_CURVE_QUADRATIC;
    current_config.dead_zone = FUZZY_DEAD_ZONE;
    current_config.turn_factor = FUZZY_TURN_FACTOR;
    current_config.max_duty = FUZZY_MAX_DUTY;
    current_config.min_duty = FUZZY_MIN_DUTY;
    current_config.invert_left = false;
    current_config.invert_right = false;
    ESP_LOGI(TAG, "Fuzzy control initialized with defaults");
}

void fuzzy_control_set_config(const fuzzy_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid config pointer");
        return;
    }
    memcpy(&current_config, config, sizeof(fuzzy_config_t));
    ESP_LOGI(TAG, "Fuzzy control configuration updated");
}

void fuzzy_control_get_config(fuzzy_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid config pointer");
        return;
    }
    memcpy(config, &current_config, sizeof(fuzzy_config_t));
}

void fuzzy_control_process(float angle, float magnitude, motor_command_t *output)
{
    if (output == NULL) {
        ESP_LOGE(TAG, "Invalid output pointer");
        return;
    }
    
    magnitude = apply_dead_zone(magnitude, current_config.dead_zone);
    if (magnitude == 0.0f) {
        output->left_duty = 0;
        output->right_duty = 0;
        return;
    }
    
    magnitude = apply_curve(magnitude, current_config.curve);
    angle = normalize_angle(angle);
    
    switch (current_config.mode) {
        case FUZZY_MODE_ARCADE:
            process_arcade_mode(angle, magnitude, &current_config, output);
            break;
        case FUZZY_MODE_TANK:
            process_tank_mode(angle, magnitude, &current_config, output);
            break;
        case FUZZY_MODE_CAR:
            process_car_mode(angle, magnitude, &current_config, output);
            break;
        case FUZZY_MODE_SMOOTH:
            process_smooth_mode(angle, magnitude, &current_config, output);
            break;
        default:
            process_arcade_mode(angle, magnitude, &current_config, output);
            break;
    }
    
    output->left_duty = apply_min_duty(output->left_duty, current_config.min_duty);
    output->right_duty = apply_min_duty(output->right_duty, current_config.min_duty);
    
    if (current_config.invert_left) output->left_duty = -output->left_duty;
    if (current_config.invert_right) output->right_duty = -output->right_duty;
    
    ESP_LOGD(TAG, "angle=%.2f, mag=%.2f -> L=%d, R=%d", angle, magnitude, output->left_duty, output->right_duty);
}

void fuzzy_control_smooth(const motor_command_t *current, const motor_command_t *target, float alpha, motor_command_t *output)
{
    if (current == NULL || target == NULL || output == NULL) {
        ESP_LOGE(TAG, "Invalid pointer in smooth function");
        return;
    }
    alpha = clamp(alpha, 0.0f, 1.0f);
    output->left_duty = (int16_t)(alpha * target->left_duty + (1.0f - alpha) * current->left_duty);
    output->right_duty = (int16_t)(alpha * target->right_duty + (1.0f - alpha) * current->right_duty);
}

void fuzzy_control_set_mode(fuzzy_control_mode_t mode)
{
    current_config.mode = mode;
    const char *mode_names[] = {"Arcade", "Tank", "Car", "Smooth"};
    ESP_LOGI(TAG, "Control mode set to: %s", mode_names[mode]);
}

void fuzzy_control_set_curve(fuzzy_curve_type_t curve)
{
    current_config.curve = curve;
    const char *curve_names[] = {"Linear", "Quadratic", "Cubic", "Sqrt"};
    ESP_LOGI(TAG, "Speed curve set to: %s", curve_names[curve]);
}

void fuzzy_control_set_inversion(bool invert_left, bool invert_right)
{
    current_config.invert_left = invert_left;
    current_config.invert_right = invert_right;
    ESP_LOGI(TAG, "Motor inversion: Left=%s, Right=%s", invert_left ? "YES" : "NO", invert_right ? "YES" : "NO");
}

void fuzzy_control_preset_gentle(void)
{
    current_config.mode = FUZZY_MODE_SMOOTH;
    current_config.curve = FUZZY_CURVE_QUADRATIC;
    current_config.dead_zone = 0.10f;
    current_config.turn_factor = 0.5f;
    current_config.max_duty = 180;
    current_config.min_duty = 40;
    ESP_LOGI(TAG, "Preset applied: GENTLE");
}

void fuzzy_control_preset_normal(void)
{
    current_config.mode = FUZZY_MODE_ARCADE;
    current_config.curve = FUZZY_CURVE_QUADRATIC;
    current_config.dead_zone = 0.08f;
    current_config.turn_factor = 0.7f;
    current_config.max_duty = 255;
    current_config.min_duty = 35;
    ESP_LOGI(TAG, "Preset applied: NORMAL");
}

void fuzzy_control_preset_aggressive(void)
{
    current_config.mode = FUZZY_MODE_TANK;
    current_config.curve = FUZZY_CURVE_LINEAR;
    current_config.dead_zone = 0.05f;
    current_config.turn_factor = 1.0f;
    current_config.max_duty = 255;
    current_config.min_duty = 30;
    ESP_LOGI(TAG, "Preset applied: AGGRESSIVE");
}

void fuzzy_control_preset_precision(void)
{
    current_config.mode = FUZZY_MODE_CAR;
    current_config.curve = FUZZY_CURVE_CUBIC;
    current_config.dead_zone = 0.08f;
    current_config.turn_factor = 0.6f;
    current_config.max_duty = 150;
    current_config.min_duty = 40;
    ESP_LOGI(TAG, "Preset applied: PRECISION");
}