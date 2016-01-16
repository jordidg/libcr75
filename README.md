# libCR75
[![Build Status](https://travis-ci.org/jordidg/libcr75.svg?branch=master)](https://travis-ci.org/jordidg/libcr75)

libCR75 implements a PCSC smartcard driver for the Transcend CR-75 51-in-1 Card Reader/Writer.

This chipset is found in multiple products, I've tested this driver using [Sitecom MD-020 All-In-One Cardreader](http://www.sitecomlearningcentre.com/products/md-020v1001/all-in-one-cardreader).

Use the following command to test if a CR-75 chipset is connected:
```lsusb | grep 1307:0361```

The driver was tested using the [Belgian eID](http://eid.belgium.be) smartcard on the following systems, other Linux distributions should work as well:
* OSX 10.11 'El Capitan'
* CentOS 6 & 7
* Ubuntu 14.04 'Trusty Tahr'

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
