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
#include "opendlv-standard-message-set.hpp"

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
      (0 == commandlineArguments.count("axis-left-updown")) ||
      (0 == commandlineArguments.count("axis-right-updown")) ) {
    std::cerr << argv[0] << " interfaces with the given PS3 controller to emit ActuationRequest messages to an OD4Session." << std::endl;
    retCode = 1;
  }
  else {
    const int32_t MIN_AXES_VALUE = -32768;
    const uint32_t MAX_AXES_VALUE = 32767;

    const bool VERBOSE{commandlineArguments.count("verbose") != 0};
    const uint8_t AXIS_LEFT_UPDOWN = std::stoi(commandlineArguments["axis-left-updown"]);
    const uint8_t AXIS_RIGHT_UPDOWN = std::stoi(commandlineArguments["axis-right-updown"]);
    const std::string DEVICE{commandlineArguments["device"]};

    const float FREQ = std::stof(commandlineArguments["freq"]);

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
      float left{0};
      float right{0};
      int32_t state{-1};
      bool hasError{false};

      // Thread to read values.
      std::thread gamepadReadingThread([&AXIS_LEFT_UPDOWN,
          &AXIS_RIGHT_UPDOWN,
          &MIN_AXES_VALUE,
          &MAX_AXES_VALUE,
          &VERBOSE,
          &valuesMutex,
          &left,
          &right,
          &state,
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
                      if (AXIS_LEFT_UPDOWN == js.number) {
                        percent = static_cast<float>(js.value - MIN_AXES_VALUE)/static_cast<float>(MAX_AXES_VALUE-MIN_AXES_VALUE);
                        left = 1.0f - 2.0f * percent;
                      }

                      if (AXIS_RIGHT_UPDOWN == js.number) {
                        percent = static_cast<float>(js.value-MIN_AXES_VALUE)/static_cast<float>(MAX_AXES_VALUE-MIN_AXES_VALUE);
                        right = 1.0f - 2.0f * percent;
                      }
                      break;
                    }
                  case JS_EVENT_BUTTON:
                    {
                      if (js.value == 1) {
                        state = js.number;
                      }
                      break;
                    }
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

      cluon::OD4Session od4{static_cast<uint16_t>(std::stoi(commandlineArguments["cid"]))};
      if (od4.isRunning()) {
        od4.timeTrigger(FREQ, [&VERBOSE, &valuesMutex, &left, &right, &state, &hasError, &od4](){
            std::lock_guard<std::mutex> lck(valuesMutex);


            if (state == 0) {
              opendlv::proxy::PedalPositionRequest pprl;
              pprl.position(left);
              od4.send(pprl, cluon::time::now(), 0);

              opendlv::proxy::PedalPositionRequest pprr;
              pprr.position(right);
              od4.send(pprr, cluon::time::now(), 10);
            }
            
            opendlv::proxy::SwitchStateRequest ssr;
            ssr.state(state);
            od4.send(ssr, cluon::time::now(), 99);

            return !hasError;
            });

        opendlv::proxy::PedalPositionRequest ppr;
        ppr.position(0.0);
        od4.send(ppr, cluon::time::now(), 0);
        od4.send(ppr, cluon::time::now(), 10);

        opendlv::proxy::SwitchStateRequest ssr;
        ssr.state(-1);
        od4.send(ssr, cluon::time::now(), 99);
      }

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

