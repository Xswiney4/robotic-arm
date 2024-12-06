/*
~~ Servo Calibration ~~

This calibrator measures the actual angle of a servo motor as we move throughout the PWM range. 
The following happens when run:
- Servo is set to the midpoint between the specified range
- PWM pulse is incrementally moved downward, monitoring the angle along the way.
- Once the angle no longer changes, we move back into the working range, and then move in finer increments
  until the motor no longer moves. The PWM signal before it stopped is recorded as the new MIN_PULSE
- AS5600 MUST BE ZERO'D AT NEW MIN_PULSE
- Then the motor moves to the other end of the range, performing the same steps as before to determine
  the other end of the PWM range; MAX_PULSE
calibratePwm():
- pwmOffset and pwmMultiplier are calculated using the new min and max pulses
calibrateError():
- The servo starts at one end of the SPECIFIED range and works it's way towards the other end of the SPECIFIED
  range, taking samples of the actual angle, and then comparing it to the supposed angle, to get the error.
- Then we create an error function, and then pull the dcOffset and dcMultiplier out of that.

*/

// Libraries
#include "pca9685.h"
#include "as5600.h"
#include "i2c.h"
#include "servo.h"

#include <chrono> // For time in ms
#include <thread> // For sleeping

#include <cstdint>  // For uint8_t and system

// I2C Config
const std::string I2C_DIRECTORY = "/dev/i2c-1"; // Define I2C directory

// PCA9685 Config
const uint8_t PCA9685_ADDR = 0x40;  // Define PCA9685 address
const uint8_t PCA9685_FREQ = 50;    // hz
const uint8_t CHANNEL = 0;          // Channel the servo will be plugged into

// Servo Config ~~~~~ ADJUST BEFORE CALIBRATING ~~~~~
const uint16_t SERVO_MIN_PULSE = 500;     // microseconds
const uint16_t SERVO_MAX_PULSE = 2500;    // microseconds
const float SERVO_MAX_ANGLE = 270;        // degrees
const float SERVO_DEFAULT_ANGLE = 135;    // degrees
const float SERVO_SPEED =  45;            // degrees/second
const float SERVO_STEP_FREQ = 50;         // hz

// AS5600 Config
const uint16_t AS5600_CONFIG = 0x0000;           // Config of AS5600

// Calibration Config
const uint8_t ROUGH_PWM_STEP = 10;  // When moving towards the endpoints, this will determine the rough step width (us)
const uint8_t FINE_PWM_STEP = 1;  // When moving towards the endpoints, this will determine the fine step width (us)

// Output Variables
float measuredMinPulse;
float measuredMaxPulse;
float pwmOffset;
float pwmMultiplier;
float dcOffset;
float dcMultiplier;

int main() {

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // ~~ Initialization ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    // Object Building
    I2C i2c(I2C_DIRECTORY);                  // Create I2C object

    PCA9685 pca9685(&i2c, PCA9685_ADDR);     // Pass pointer to I2C object and address
    pca9685.setPWMFrequency(PCA9685_FREQ);   // Set output frequency of PWM signal

    AS5600 as5600(&i2c, AS5600_CONFIG);      // AS5600 Object

    ServoParams servoParams = {&pca9685, CHANNEL, SERVO_MIN_PULSE, SERVO_MAX_PULSE
							 , SERVO_MAX_ANGLE, SERVO_DEFAULT_ANGLE, SERVO_SPEED, SERVO_STEP_FREQ};
    
    Servo servo(servoParams);

    // Set to midpoint
    uint16_t MID_PULSE = (SERVO_MAX_PULSE + SERVO_MIN_PULSE) / 2;
    pca9685.setPulseWidth(CHANNEL, MID_PULSE);

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // ~~ Determine Lower End ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    
    // Gets the current rotational step (0 - 4095)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    uint16_t lastAbsoluteStep = as5600.getStep();
    uint16_t absoluteStep;

    float currentPulseWidth;
    
    // Rough Measurement - moves downward until motor stops
    // Increments currentPulseWidth downward with the end condition being that currentPulseWidth can't be less than 0
    for(currentPulseWidth = MID_PULSE - ROUGH_PWM_STEP; currentPulseWidth > 0; currentPulseWidth -= ROUGH_PWM_STEP){
      
      // Sets channel one ROUGH increment down
      pca9685.setPulseWidth(CHANNEL, currentPulseWidth);

      // Give motor time to move and settle
      // ~4.5ms to move motor (BASED ON 10us STEPS) + 2.2ms ASM5600 delay + 3.3ms buffer = 10ms
      std::this_thread::sleep_for(std::chrono::milliseconds(10));

      // Gets the absolute rotation
      absoluteStep = as5600.getStep();

      // This condition triggers if the absolute step does NOT change
      if(absoluteStep == lastAbsoluteStep){
        break;
      }
      else{
        lastAbsoluteStep = absoluteStep;
      }
    }

    // Now we go back 2 steps and then move in finer steps
    currentPulseWidth = currentPulseWidth + (2 * ROUGH_PWM_STEP);
    pca9685.setPulseWidth(CHANNEL, currentPulseWidth);
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    uint16_t lastAbsoluteStep = as5600.getStep();

    // Fine Measurement - moves downward until motor stops
    
    // Increments currentPulseWidth downward with the end condition being that currentPulseWidth can't be less than 0
    for(currentPulseWidth = currentPulseWidth - FINE_PWM_STEP; currentPulseWidth > 0; currentPulseWidth -= FINE_PWM_STEP){
      
      // Sets channel one ROUGH increment down
      pca9685.setPulseWidth(CHANNEL, currentPulseWidth);

      // Give motor time to move and settle
      // ~.45ms to move motor (BASED ON 1us STEPS) + 2.2ms ASM5600 delay + 2.35ms buffer = 5ms
      std::this_thread::sleep_for(std::chrono::milliseconds(5));

      // Gets the absolute rotation
      absoluteStep = as5600.getStep();

      // This condition triggers if the absolute step does NOT change
      if(absoluteStep == lastAbsoluteStep){
        break;
      }
      else{
        lastAbsoluteStep = absoluteStep;
      }
    }

    // Now we go back 2 steps and record that as the minimum pulse width
    currentPulseWidth = currentPulseWidth + (2 * FINE_PWM_STEP);
    pca9685.setPulseWidth(CHANNEL, currentPulseWidth);
    
    measuredMinPulse = currentPulseWidth;

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // ~~ Zero AS5600 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    as5600.zero();

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // ~~ Determine Higher End ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    // Set to midpoint
    pca9685.setPulseWidth(CHANNEL, MID_PULSE);
    currentPulseWidth = MID_PULSE;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Gets the current rotational step (0 - 4095)
    lastAbsoluteStep = as5600.getStep();
    
    // Rough Measurement - moves downward until motor stops
    // Increments currentPulseWidth downward with the end condition being that currentPulseWidth can't greater than MAX_PULSE + 1000
    for(currentPulseWidth = MID_PULSE + ROUGH_PWM_STEP; currentPulseWidth < SERVO_MAX_PULSE + 1000; currentPulseWidth += ROUGH_PWM_STEP){
      
      // Sets channel one ROUGH increment down
      pca9685.setPulseWidth(CHANNEL, currentPulseWidth);

      // Give motor time to move and settle
      // ~4.5ms to move motor (BASED ON 10us STEPS) + 2.2ms ASM5600 delay + 3.3ms buffer = 10ms
      std::this_thread::sleep_for(std::chrono::milliseconds(10));

      // Gets the absolute rotation
      absoluteStep = as5600.getStep();

      // This condition triggers if the absolute step does NOT change
      if(absoluteStep == lastAbsoluteStep){
        break;
      }
      else{
        lastAbsoluteStep = absoluteStep;
      }
    }

    // Now we go back 2 steps and then move in finer steps
    currentPulseWidth = currentPulseWidth - (2 * ROUGH_PWM_STEP);
    pca9685.setPulseWidth(CHANNEL, currentPulseWidth);
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    uint16_t lastAbsoluteStep = as5600.getStep();

    // Fine Measurement - moves downward until motor stops
    
    // Increments currentPulseWidth downward with the end condition being that currentPulseWidth can't be less than 0
    for(currentPulseWidth = currentPulseWidth + FINE_PWM_STEP; currentPulseWidth < SERVO_MAX_PULSE + 1000; currentPulseWidth += FINE_PWM_STEP){
      
      // Sets channel one ROUGH increment down
      pca9685.setPulseWidth(CHANNEL, currentPulseWidth);

      // Give motor time to move and settle
      // ~.45ms to move motor (BASED ON 1us STEPS) + 2.2ms ASM5600 delay + 2.35ms buffer = 5ms
      std::this_thread::sleep_for(std::chrono::milliseconds(5));

      // Gets the absolute rotation
      absoluteStep = as5600.getStep();

      // This condition triggers if the absolute step does NOT change
      if(absoluteStep == lastAbsoluteStep){
        break;
      }
      else{
        lastAbsoluteStep = absoluteStep;
      }
    }

    // Now we go back 2 steps and record that as the maximum pulse width
    currentPulseWidth = currentPulseWidth - (2 * FINE_PWM_STEP);
    pca9685.setPulseWidth(CHANNEL, currentPulseWidth);
    
    measuredMaxPulse = currentPulseWidth;

    std::this_thread::sleep_for(std::chrono::milliseconds(10));


    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // ~~ Calculate pwmOffset and pwmMultiplier ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    
    float measuredMidPulse = (measuredMaxPulse + measuredMinPulse) / 2;

    pwmOffset = measuredMidPulse - MID_PULSE;
    pwmMultiplier = measuredMaxPulse / (SERVO_MAX_PULSE + pwmOffset);

    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // ~~ Sample Entire Range ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~



    // ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    // ~~ Calculate dcOffset and dcMultiplier ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


    return 0;
}