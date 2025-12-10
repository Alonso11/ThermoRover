/**
 * @file fuzzy_control.h
 * @brief Fuzzy logic control for joystick to differential drive mapping
 * 
 * Purpose:
 * Convert joystick polar coordinates (angle, magnitude) into left/right motor
 * speeds for smooth, intuitive differential drive control.
 * 
 * Control Philosophy:
 * - Forward motion: Both motors forward, proportional to magnitude
 * - Turning: Reduce inner wheel speed based on turn angle
 * - Backward motion: Both motors backward
 * - Rotation in place: Opposite motor directions at high turn angles
 */

#ifndef FUZZY_CONTROL_H
#define FUZZY_CONTROL_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// CONTROL PARAMETERS
// ============================================================================

/**
 * @brief Dead zone threshold (0.0 to 1.0)
 * 
 * Decision: Small dead zone prevents drift from joystick centering errors
 * Joystick magnitudes below this value are treated as zero
 * Typical value: 0.05 to 0.10 (5-10%)
 */
#define FUZZY_DEAD_ZONE           0.08f

/**
 * @brief Maximum motor duty cycle (0-255)
 * 
 * Decision: Allows limiting maximum speed for safety or battery life
 * Can be adjusted based on battery voltage or user preference
 */
#define FUZZY_MAX_DUTY            255

/**
 * @brief Minimum motor duty cycle to overcome static friction
 * 
 * Decision: Motors need minimum PWM to start moving
 * Below this value, motor may stall or not move at all
 * Typical value: 30-50 for DC motors
 */
#define FUZZY_MIN_DUTY            35

/**
 * @brief Turn aggressiveness factor (0.0 to 1.0)
 * 
 * Decision: Controls how much to reduce inner wheel during turns
 * - 0.0 = No differential (both wheels same speed, poor turning)
 * - 0.5 = Moderate differential (smooth car-like turns)
 * - 1.0 = Maximum differential (aggressive tank-like turns)
 */
#define FUZZY_TURN_FACTOR         0.7f

// ============================================================================
// CONTROL MODES
// ============================================================================

/**
 * @brief Control mode enumeration
 * 
 * Decision: Multiple modes allow user preference or different scenarios
 */
typedef enum {
    FUZZY_MODE_ARCADE,      // Arcade: Magnitude = speed, Angle = turn rate
    FUZZY_MODE_TANK,        // Tank: Separate control of left/right tracks
    FUZZY_MODE_CAR,         // Car: Reduce inner wheel, like Ackermann steering
    FUZZY_MODE_SMOOTH,      // Smooth: Gradual transitions, least aggressive
} fuzzy_control_mode_t;

/**
 * @brief Speed curve type
 * 
 * Decision: Different curves for different control feel
 */
typedef enum {
    FUZZY_CURVE_LINEAR,     // Direct 1:1 mapping
    FUZZY_CURVE_QUADRATIC,  // Gentler at low speeds, more control precision
    FUZZY_CURVE_CUBIC,      // Even gentler start, exponential at high speeds
    FUZZY_CURVE_SQRT,       // Faster response at low speeds
} fuzzy_curve_type_t;

// ============================================================================
// CONFIGURATION STRUCTURE
// ============================================================================

/**
 * @brief Fuzzy control configuration
 * 
 * Decision: Grouping all parameters in a structure allows
 * easy runtime adjustment and multiple profiles
 */
typedef struct {
    fuzzy_control_mode_t mode;     // Control mode
    fuzzy_curve_type_t curve;      // Speed response curve
    float dead_zone;               // Dead zone threshold (0.0-1.0)
    float turn_factor;             // Turn aggressiveness (0.0-1.0)
    int16_t max_duty;              // Maximum motor duty (0-255)
    int16_t min_duty;              // Minimum motor duty to start moving
    bool invert_left;              // Invert left motor direction
    bool invert_right;             // Invert right motor direction
} fuzzy_config_t;

// ============================================================================
// OUTPUT STRUCTURE
// ============================================================================

/**
 * @brief Motor command output
 * 
 * Decision: Separate structure for output makes the API clear
 * and allows future expansion (e.g., acceleration limits)
 */
typedef struct {
    int16_t left_duty;   // Left motor duty cycle (-255 to +255)
    int16_t right_duty;  // Right motor duty cycle (-255 to +255)
} motor_command_t;

// ============================================================================
// FUNCTION PROTOTYPES
// ============================================================================

/**
 * @brief Initialize fuzzy control system with default configuration
 * 
 * Decision: Provides sensible defaults for immediate use
 * Users can customize via fuzzy_control_set_config()
 */
void fuzzy_control_init(void);

/**
 * @brief Set custom fuzzy control configuration
 * 
 * @param config Pointer to configuration structure
 */
void fuzzy_control_set_config(const fuzzy_config_t *config);

/**
 * @brief Get current fuzzy control configuration
 * 
 * @param config Pointer to store current configuration
 */
void fuzzy_control_get_config(fuzzy_config_t *config);

/**
 * @brief Process joystick input and generate motor commands
 * 
 * This is the main function that implements the fuzzy logic control.
 * 
 * Joystick coordinate system:
 *        Forward (π/2, 90°)
 *               ↑
 *               |
 *   Left ←------+------→ Right
 *  (π,180°)     |     (0°, 0)
 *               |
 *               ↓
 *        Back (3π/2, 270°)
 * 
 * @param angle Joystick angle in radians (0 to 2π)
 * @param magnitude Joystick magnitude (0.0 to 1.0)
 * @param output Pointer to store motor commands
 */
void fuzzy_control_process(float angle, float magnitude, motor_command_t *output);

/**
 * @brief Apply exponential smoothing to motor commands (optional)
 * 
 * Decision: Smoothing reduces jerky movements and mechanical stress
 * Useful for wireless control where commands may be delayed/bursty
 * 
 * @param current Current motor command
 * @param target Target motor command
 * @param alpha Smoothing factor (0.0-1.0, higher = faster response)
 * @param output Pointer to store smoothed command
 */
void fuzzy_control_smooth(const motor_command_t *current,
                         const motor_command_t *target,
                         float alpha,
                         motor_command_t *output);

/**
 * @brief Set control mode (arcade, tank, car, smooth)
 * 
 * @param mode Desired control mode
 */
void fuzzy_control_set_mode(fuzzy_control_mode_t mode);

/**
 * @brief Set speed response curve
 * 
 * @param curve Desired curve type
 */
void fuzzy_control_set_curve(fuzzy_curve_type_t curve);

/**
 * @brief Enable/disable motor direction inversion
 * 
 * Decision: Allows correcting for reversed motor wiring without hardware changes
 * 
 * @param invert_left Invert left motor direction
 * @param invert_right Invert right motor direction
 */
void fuzzy_control_set_inversion(bool invert_left, bool invert_right);

/**
 * @brief Quick preset configurations for common use cases
 */
void fuzzy_control_preset_gentle(void);    // Smooth, beginner-friendly
void fuzzy_control_preset_normal(void);    // Balanced control
void fuzzy_control_preset_aggressive(void); // Fast, responsive, expert mode
void fuzzy_control_preset_precision(void);  // Fine control, limited speed

#endif // FUZZY_CONTROL_H