## OpenDLV Microservice to interface with a gamepad such as a PS3 or PS4 controller

This repository provides source code to interface with a gamepad such as a PS3
or PS4 controller to broadcast ActuationRequest messages.

[![Build Status](https://travis-ci.org/chalmers-revere/opendlv-device-gamepad.svg?branch=master)](https://travis-ci.org/chalmers-revere/opendlv-device-gamepad) [![License: GPLv3](https://img.shields.io/badge/license-GPL--3-blue.svg
)](https://www.gnu.org/licenses/gpl-3.0.txt)


## Table of Contents
* [Dependencies](#dependencies)
* [Usage](#usage)
* [Build from sources on the example of Ubuntu 16.04 LTS](#build-from-sources-on-the-example-of-ubuntu-1604-lts)
* [License](#license)


## Dependencies
You need a C++14-compliant compiler to compile this project. The following
dependency is shipped as part of the source distribution:

* [libcluon](https://github.com/chrberger/libcluon) - [![License: GPLv3](https://img.shields.io/badge/license-GPL--3-blue.svg
)](https://www.gnu.org/licenses/gpl-3.0.txt)


## Usage
This microservice is created automatically on changes to this repository via Docker's public registry for:
* [x86_64](https://hub.docker.com/r/chalmersrevere/opendlv-device-gamepad-amd64/tags/)
* [armhf](https://hub.docker.com/r/chalmersrevere/opendlv-device-gamepad-armhf/tags/)
* [aarch64](https://hub.docker.com/r/chalmersrevere/opendlv-device-gamepad-aarch64/tags/)

To run this microservice using our pre-built Docker multi-arch images to open
a PS3 Controller, simply start it as follows:

```
docker run --rm -ti --init --net=host --device /dev/input/js0 chalmersrevere/opendlv-device-gamepad-multi:v0.0.10 --device=/dev/input/js0 --axis_leftright=0 --axis_updown=4 --freq=100 --acc_min=0 --acc_max=50 --dec_min=0 --dec_max=-10 --steering_min=-10 --steering_max=10 --steering_max_rate=5.0 --cid=111 --verbose
```

To run this microservice using our pre-built Docker multi-arch images to open
a PS4 Controller, simply start it as follows:

```
docker run --rm -ti --init --net=host --device /dev/input/js0 chalmersrevere/opendlv-device-gamepad-multi:v0.0.10 --device=/dev/input/js0 --axis_leftright=0 --axis_updown=5 --freq=100 --acc_min=0 --acc_max=50 --dec_min=0 --dec_max=-10 --steering_min=-10 --steering_max=10 --steering_max_rate=5.0 --cid=111 --verbose
```

## Build from sources on the example of Ubuntu 16.04 LTS
To build this software, you need cmake, C++14 or newer, libx11-dev, and make.
Having these preconditions, just run `cmake` and `make` as follows:

```
mkdir build && cd build
cmake -D CMAKE_BUILD_TYPE=Release ..
make && make test && make install
```


## License

* This project is released under the terms of the GNU GPLv3 License

