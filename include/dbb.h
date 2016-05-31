// Copyright (c) 2015 Jonas Schnelli
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <functional>
#include <stdio.h>
#include <string>
#include <vector>

#define HID_REPORT_SIZE_DEFAULT 4096
#define HID_BL_BUF_SIZE_W 4098
#define HID_BL_BUF_SIZE_R 256
#define FIRMWARE_CHUNKSIZE 4096
#define DBB_APP_LENGTH 225280 //flash size minus bootloader length

#define BACKUP_KEY_PBKDF2_SALT     "Digital Bitbox"
#define BACKUP_KEY_PBKDF2_SALTLEN  14
#define BACKUP_KEY_PBKDF2_ROUNDS   20480
#define BACKUP_KEY_PBKDF2_HMACLEN  64

namespace DBB {

enum dbb_device_mode {
    DBB_DEVICE_NO_DEVICE = 0,
    DBB_DEVICE_MODE_BOOTLOADER,
    DBB_DEVICE_MODE_FIRMWARE,
    DBB_DEVICE_MODE_FIRMWARE_NO_PASSWORD,
    DBB_DEVICE_UNKNOWN,
};
//!open a connection to the digital bitbox device
// retruns false if no connection could be made, keeps connection handling
// internal
bool openConnection(unsigned int writeBufSizeIn = HID_REPORT_SIZE_DEFAULT, unsigned int readBufSizeIn = HID_REPORT_SIZE_DEFAULT);

//!close the connection to the dbb device
bool closeConnection();

//!check if a DBB device is available
enum dbb_device_mode deviceAvailable();

//!return true if a USBHID connection is open
bool isConnectionOpen();

//!send a json command to the device which is currently open
bool sendCommand(const std::string &json, std::string &resultOut);

//!send a binary chunk (used for firmware updates)
bool sendChunk(unsigned int chunknum, const std::vector<unsigned char>& data, std::string& resultOut);

//!send firmware
bool upgradeFirmware(const std::vector<char>& firmware, size_t firmwareSize, const std::string& sigCmpStr, std::function<void(const std::string&, float)> progressCallback);

//!decrypt a json result
bool decryptAndDecodeCommand(const std::string &cmdIn,
                             const std::string &password,
                             std::string &stringOut);

//!encrypts a json command
bool encryptAndEncodeCommand(const std::string &cmd,
                             const std::string &password,
                             std::string &base64strOut);

std::string getStretchedBackupHexKey(const std::string &passphrase);
} //end namespace DBB
