# libCR75
[![Build Status](https://travis-ci.org/jordidg/libcr75.svg?branch=master)](https://travis-ci.org/jordidg/libcr75)

libCR75 implements a PCSC smartcard driver for the Transcend CR-75 51-in-1 Card Reader/Writer.

Use the following command to test if a CR-75 chipset is connected:
```lsusb | grep 1307:0361```

## Installation
No binary packages are provided, follow the instructions below to build from source.

## Building
libCR75 requires the following libraries:
* [libusb1](http://libusb.info)
* [pcsclite](http://pcsclite.alioth.debian.org/pcsclite.html) - on OSX this library is bundled with the OS

```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
make install
```
