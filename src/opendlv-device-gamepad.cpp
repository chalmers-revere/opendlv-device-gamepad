/*
 * Copyright (C) 2018  Christian Berger
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "cluon-complete.hpp"
#include "actuationrequestmessage.hpp"

#include <linux/joystick.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

int32_t main(int32_t argc, char **argv) {
    int32_t retCode{0};
    auto commandlineArguments = cluon::getCommandlineArguments(argc, argv);
    if ( (0 == commandlineArguments.count("cid")) ||
         (0 == commandlineArguments.count("device")) ||
         (0 == commandlineArguments.count("freq")) ||
         (0 == commandlineArguments.count("axis_leftright")) ||
         (0 == commandlineArguments.count("axis_updown")) ||
         (0 == commandlineArguments.count("acc_min")) ||
         (0 == commandlineArguments.count("acc_max")) ||
         (0 == commandlineArguments.count("dec_min")) ||
         (0 == commandlineArguments.count("dec_max")) ||
         (0 == commandlineArguments.count("steering_min")) ||
         (0 == commandlineArguments.count("steering_max")) ) {
        std::cerr << argv[0] << " interfaces with the given PS3 controller to emit ActuationRequest messages to an OD4Session." << std::endl;
        std::cerr << "Usage:   " << argv[0] << " --device=<PS3 controller device> --freq=<frequency in Hz>--acc_min=<minimum acceleration> --acc_max=<maximum acceleration> --dec_min=<minimum deceleration> --dec_max=<maximum deceleration> --steering_min=<minimum steering> --steering_max=<maximum steering> [--steering_max_rate=5.0] --cid=<OpenDaVINCI session> [--ps4] [--verbose]" << std::endl;
        std::cerr << "Example: " << argv[0] << " --device=/dev/input/js0 --axis_leftright=0 --axis_updown=4 --freq=100 --acc_min=0 --acc_max=50 --dec_min=0 --dec_max=-10 --steering_min=-10 --steering_max=10 --steering_max_rate=5.0 --cid=111" << std::endl;
        retCode = 1;
    }
    else {
        const int32_t MIN_AXES_VALUE = -32768;
        const uint32_t MAX_AXES_VALUE = 32767;

        const bool VERBOSE{commandlineArguments.count("verbose") != 0};
        const uint8_t AXIS_LEFTRIGHT = std::stoi(commandlineArguments["axis_leftright"]);
        const uint8_t AXIS_UPDOWN = std::stoi(commandlineArguments["axis_updown"]);
        const std::string DEVICE{commandlineArguments["device"]};

        const float FREQ = std::stof(commandlineArguments["freq"]);
        const float ACCELERATION_MIN = std::stof(commandlineArguments["acc_min"]);
        const float ACCELERATION_MAX = std::stof(commandlineArguments["acc_max"]);
        const float DECELERATION_MIN = std::stof(commandlineArguments["dec_min"]);
        const float DECELERATION_MAX = std::stof(commandlineArguments["dec_max"]);
        const float STEERING_MIN = std::stof(commandlineArguments["steering_min"]);
        const float STEERING_MAX = std::stof(commandlineArguments["steering_max"]);

        float const STEERING_MAX_RATE = (commandlineArguments.count("steering_max_rate") != 0) ? std::stof(commandlineArguments["steering_max_rate"]) : -1.0f;
        float const TS = 1.0f / FREQ;

        int gamepadDevice;
        if ( -1 == (gamepadDevice = ::open(DEVICE.c_str(), O_RDONLY)) ) {
            std::cerr << "[opendlv-device-gamepad]: Could not open device: " << DEVICE << ", error: " << errno << ": " << strerror(errno) << std::endl;
        }
        else {
            int num_of_axes{0};
            int num_of_buttons{0};
            char name_of_gamepad[80];

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverflow"
            ::ioctl(gamepadDevice, JSIOCGAXES, &num_of_axes);
            ::ioctl(gamepadDevice, JSIOCGBUTTONS, &num_of_buttons);
#pragma GCC diagnostic pop
            if (::ioctl(gamepadDevice, JSIOCGNAME(80), &name_of_gamepad) < 0) {
                ::strncpy(name_of_gamepad, "Unknown", sizeof(name_of_gamepad));
            }
            std::clog << "[opendlv-device-gamepad]: Found " << std::string(name_of_gamepad) << ", number of axes: " << num_of_axes << ", number of buttons: " << num_of_buttons << std::endl;

            // Use non blocking reading.
            fcntl(gamepadDevice, F_SETFL, O_NONBLOCK);

            std::mutex valuesMutex;
            float acceleration{0};
            float steering{0};
            float targetSteering{0};
            float prevSteering{0};
            bool hasError{false};

            // Thread to read values.
            std::thread gamepadReadingThread([&AXIS_LEFTRIGHT,
                                              &AXIS_UPDOWN,
                                              &MIN_AXES_VALUE,
                                              &MAX_AXES_VALUE,
                                              &VERBOSE,
                                              &ACCELERATION_MIN,
                                              &ACCELERATION_MAX,
                                              &DECELERATION_MIN,
                                              &DECELERATION_MAX,
                                              &STEERING_MIN,
                                              &STEERING_MAX,
                                              &STEERING_MAX_RATE,
                                              &valuesMutex,
                                              &acceleration,
                                              &steering,
                                              &targetSteering,
                                              &hasError,
                                              &gamepadDevice]() {
                struct timeval timeout {};
                fd_set setOfFiledescriptorsToReadFrom{};

                while (!hasError) {
                    // Define timeout for select system call. The timeval struct must be
                    // reinitialized for every select call as it might be modified containing
                    // the actual time slept.
                    timeout.tv_sec  = 0;
                    timeout.tv_usec = 20 * 1000; // Check for new data with 50Hz.

                    FD_ZERO(&setOfFiledescriptorsToReadFrom);
                    FD_SET(gamepadDevice, &setOfFiledescriptorsToReadFrom);
                    ::select(gamepadDevice + 1, &setOfFiledescriptorsToReadFrom, nullptr, nullptr, &timeout);

                    if (FD_ISSET(gamepadDevice, &setOfFiledescriptorsToReadFrom)) {
                        std::lock_guard<std::mutex> lck(valuesMutex);

                        struct js_event js;
                        while (::read(gamepadDevice, &js, sizeof(struct js_event)) > 0) {
                            float percent{0};
                            switch (js.type & ~JS_EVENT_INIT) {
                                case JS_EVENT_AXIS:
                                {
                                    if (AXIS_LEFTRIGHT == js.number) { // LEFT ANALOG STICK
                                        // this will return a percent value over the whole range
                                        percent = static_cast<float>(js.value - MIN_AXES_VALUE)/static_cast<float>(MAX_AXES_VALUE-MIN_AXES_VALUE)*100.0f;

                                        if (VERBOSE) {
                                            if (percent > 49.95f && percent < 50.05f) {
                                                std::cout << "[opendlv-device-gamepad]: Going straight." << std::endl;
                                            }
                                            else {
                                                // this will return values in the range [0-100] for both a left or right turn (instead of [0-50] for left and [50-100] for right)
                                                std::cout << "[opendlv-device-gamepad]: Turning "<< (js.value<0?"left":"right") << " at " << (js.value<0?(100.0f-2.0f*percent):(2.0f*percent-100.0f)) <<"%." << std::endl;
                                            }
                                        }

                                        // map the steering from percentage to its range
                                        steering = percent/100.0f*(STEERING_MAX-STEERING_MIN)+STEERING_MIN;
                                        steering *= -1.0f;
                                        // modify in steps of 0.25
                                        steering = ::roundf(4.0f*steering)/4.0f;
                                        
                                        // Clamp value to avoid showing "-0" (just "0" looks better imo)
                                        if (steering < 0.001f && steering >-0.001f) {
                                            steering = 0;
                                        }

                                        if (STEERING_MAX_RATE > 0.0f) {
                                            targetSteering = steering;
                                        }
                                    }
                                    // no else-if as many of these events can occur simultaneously
                                    if (AXIS_UPDOWN == js.number) { // RIGHT ANALOG STICK
                                        // this will return a percent value over the whole range
                                        percent = static_cast<float>(js.value-MIN_AXES_VALUE)/static_cast<float>(MAX_AXES_VALUE-MIN_AXES_VALUE)*100.0f;
                                        // this will return values in the range [0-100] for both accelerating and braking (instead of [50-0] for accelerating and [50-100] for braking)
                                        if (VERBOSE) {
                                            std::cout << "[opendlv-device-gamepad]: " << (js.value<0?"Accelerating":"Braking") <<" at "<< (js.value<0?(100.0f-2.0f*percent):(2.0f*percent-100.0f)) << "%." << std::endl;
                                        }

                                        if (js.value < 0) {
                                            // map the acceleration from percentage to its range
                                            acceleration=(100.0f-2.0f*percent)/100.0f*(ACCELERATION_MAX-ACCELERATION_MIN)+ACCELERATION_MIN;
                                        }
                                        else {
                                            // map the acceleration from percentage to its range
                                            acceleration = (2.0f*percent-100.0f)/100.0f*(DECELERATION_MAX-DECELERATION_MIN);
                                        }

                                        // modify in steps of 0.25
                                        acceleration = ::roundf(4.0f*acceleration)/4.0f;

                                        // Clamp value to avoid showing "-0" (just "0" looks better imo)
                                        if (acceleration < 0.001f && acceleration >-0.001f) {
                                            acceleration = 0;
                                        }
                                    }
                                    break;
                                }
                                case JS_EVENT_BUTTON:
                                    break;
                                case JS_EVENT_INIT:
                                    break;
                                default:
                                    break;
                            }
                        }
                        if (errno != EAGAIN) {
                            std::cerr << "[opendlv-device-gamepad]: Error: " << errno << ": " << strerror(errno) << std::endl;
                            hasError = true;
                        }
                    }
                }
            });

            // OD4Session to send values to.
            opendlv::proxy::ActuationRequest ar;
            cluon::OD4Session od4{static_cast<uint16_t>(std::stoi(commandlineArguments["cid"]))};
            if (od4.isRunning()) {
                od4.timeTrigger(FREQ, [&VERBOSE,
                                       &STEERING_MAX_RATE,
                                       &TS,
                                       &valuesMutex,
                                       &acceleration,
                                       &steering,
                                       &prevSteering,
                                       &targetSteering,
                                       &hasError,
                                       &ar,
                                       &od4](){
                    std::lock_guard<std::mutex> lck(valuesMutex);

                    if (STEERING_MAX_RATE > 0.0f) {
                        float inc = TS * STEERING_MAX_RATE;
                        float steeringRate = (steering - prevSteering) / TS;
                        if (steeringRate > STEERING_MAX_RATE) {
                            steering = prevSteering + inc;
                        } else if (steeringRate < -STEERING_MAX_RATE) {
                            steering = prevSteering - inc;
                        }
                    }

                    prevSteering = steering;
                    ar.acceleration(acceleration).steering(steering).isValid(!hasError);

                    if (VERBOSE) {
                        std::stringstream buffer;
                        ar.accept([](uint32_t, const std::string &, const std::string &) {},
                                 [&buffer](uint32_t, std::string &&, std::string &&n, auto v) { buffer << n << " = " << v << '\n'; },
                                 []() {});
                        std::cout << buffer.str() << std::endl;
                    }
                    od4.send(ar);

                    if (STEERING_MAX_RATE > 0.0f) {
                        steering = targetSteering;
                    }

                    // Determine whether to continue or not.
                    return !hasError;
                });

                // Send stop.
                ar.acceleration(0).steering(0).isValid(true);
                od4.send(ar);
            }

            // Stop thread.
            {
                std::lock_guard<std::mutex> lck(valuesMutex);
                hasError = true;
                gamepadReadingThread.join();
            }

            ::close(gamepadDevice);
            retCode = 0;
        }
    }
    return retCode;
}

