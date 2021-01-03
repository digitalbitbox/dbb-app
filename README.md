[![License](http://img.shields.io/:License-MIT-yellow.svg)](LICENSE)
[![Build Status](https://travis-ci.org/digitalbitbox/dbb-app.svg?branch=master)](https://travis-ci.org/digitalbitbox/dbb-app)

# WARNING: This is an UNMAINTAINED predecessor of the [BitBoxApp](https://github.com/digitalbitbox/bitbox-wallet-app/)

## DBB-APP
A QT based application for the [Digital Bitbox](https://digitalbitbox.com) hardware wallet. The application support managing your dbb device (create new wallet, backup, set 2FA key, etc.). It also supports co-signing together with a [Bitpay Copay Wallet](http://copay.io).

## dbb-app
The dbb-app Qt app will be built if qt5 is available on your system or if `--with-gui=qt5` has been set. 

## dbb-cli
This package includes a cli tool "dbb-cli" which can be used to direcly talk with your Digital Bitbox.


**Examples:**

* `dbb-cli help`
* `dbb-cli -newpassword=0000 password`
* `dbb-cli -newpassword=test -password=0000 password`
* `dbb-cli -password=test led`
* `dbb-cli -password=test seed`
* `dbb-cli -password=test -keypath=m/44/0 xpub`
* `dbb-cli -password=test erase`

Below are available commands with possible arguments (`*` indicates mandatory). Refer to the [API documentation](https://digitalbitbox.com/api) for explanations about each command.

```
  erase 
  password -*newpassword
  led 
  seed -source (default: create), -raw (default: false), -entropy (default: 0123456789abcde), -key (default: 1234), -filename (default: backup.dat)
  backuplist 
  backuperaseall 
  backuperasefile -*filename
  backup -key (default: 1234), -filename (default: backup.dat)
  sign -lock_pin (default: 1234), -meta (default: meta), -*hasharray, -pubkeyarray

    Example
    This signs the given hash(es) with the private key specified at the BIP32/44 keypath. The command must be sent two times in order to sign - refer to the API documentation. It also optionally checks if a pubkey matches the pubkey derived at the given keypath.
    =======
    dbb-cli --password=0000 sign -hasharray='[{"hash": "f6f4a3633eda92eef9dd96858dec2f5ea4dfebb67adac879c964194eb3b97d79", "keypath":"m/44/0"}]' -pubkeyarray='[{"pubkey":"0270526bf580ddb20ad18aad62b306d4beb3b09fae9a70b2b9a93349b653ef7fe9", "keypath":"m/44"}]' -meta=34982a264657cdf2635051bd778c99e73ce5eb2e8c7f9d32b8aaa7e547c7fd90

  xpub -*keypath
  random -mode (default: true)
  info 
  lock 
  verifypass -operation (default: create)
  aes -type (default: encrypt), -*data
  bootloaderunlock 
  bootloaderlock 
  firmware -filename
  hidden_password -*hiddenpassword 
```

## Build Instructions
### Dependencies:

dbb-cli and dbb-app depend on libbtc (https://github.com/libbtc/libbtc) for key generation, signing, hashing, crypto, etc.
Libbtc is included as git subtree and will be compiled during the normal build process.

- libbtc (included as git subtree) (https://github.com/libbtc/libbtc)
- hidapi (included as git subtree) (https://github.com/signal11/hidapi)
- libcurl (https web calls)
- libqrencode (QR Code generation)
- qt5 (if UI enabled)

OSX:

    brew install hidapi libevent qt5

#### Linux (Ubuntu 15.04):

    sudo apt-get install build-essential libtool autotools-dev autoconf pkg-config git
    sudo apt-get install libusb-1.0-0-dev libcurl4-openssl-dev libudev-dev libqrencode-dev
    sudo apt-get install libqt5gui5 libqt5core5a libqt5dbus5 qttools5-dev qttools5-dev-tools libqt5websockets5-dev libavahi-compat-libdnssd-dev


#### Linux (Fedora 21/22):

    sudo yum groupinstall "C Development Tests and Libraries"
    sudo yum install git hidapi-devel
    sudo yum install mesa-libGL-devel qt5-qttools-devel


#### *nix depends secure build (from hash-verified source)

Optionally probably want to create out-of-tree directory to save latest source & built binaries:

    mkdir -p deps-cache/sources/

    git clone https://github.com/digitalbitbox/dbb-app.git
    cd dbb-app/depends
    make -j4 BASE_CACHE=(absolute-path-here)/deps-cache/ SOURCES_PATH=(absolute-path-here)/deps-cache/sources/
    cd ..
    ./autogen.sh
    ./configure --prefix=`pwd`/depends/x86_64-pc-linux-gnu --enable-debug --with-gui=qt5 --enable-libusb
    make



### Basic build steps:

    ./autogen.sh
    ./configure --enable-debug --with-gui=qt5 --enable-libusb
    make
    sudo make install
