/*

 The MIT License (MIT)

 Copyright (c) 2017 Jonas Schnelli

 Permission is hereby granted, free of charge, to any person obtaining
 a copy of this software and associated documentation files (the "Software"),
 to deal in the Software without restriction, including without limitation
 the rights to use, copy, modify, merge, publish, distribute, sublicense,
 and/or sell copies of the Software, and to permit persons to whom the
 Software is furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included
 in all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
 OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 OTHER DEALINGS IN THE SOFTWARE.

*/

#include <assert.h>
#include <fstream>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <cmath>

#include <string>

#include "dbb.h"
#include "dbb_util.h"

#include "hidapi/hidapi.h"

#include <btc/hash.h>
#include <btc/ecc_key.h>
#include <btc/ecc.h>

#include "firmware.h"

#include <sstream>
#include <iostream>

std::string sendSingleCommand(const std::string& cmd, const std::string &password)
{
    DebugOut("main", "encrypting raw json: %s\n", cmd.c_str());
    std::string base64str;
    std::string cmdOut;
    std::string unencryptedJson;
    DBB::encryptAndEncodeCommand(cmd, password, base64str);
    DBB::sendCommand(base64str, cmdOut);
    DBB::decryptAndDecodeCommand(cmdOut, password, unencryptedJson);
    return unencryptedJson;
}

void upgradeFirmware(const std::string& password, const std::string& possibleFilename) {

    std::streamsize firmwareSize;
    std::stringstream buffer;

    if (possibleFilename.empty() || possibleFilename == "")
    {
        // load internally
        for (int i = 0; i<firmware_deterministic_2_0_0_signed_bin_len;i++)
        {
            buffer << firmware_deterministic_2_0_0_signed_bin[i];
        }
        firmwareSize = firmware_deterministic_2_0_0_signed_bin_len;
    }
    else {
        std::ifstream firmwareFile(possibleFilename, std::ios::binary);
        buffer << firmwareFile.rdbuf();
        firmwareSize = firmwareFile.tellg();
        firmwareFile.close();
    }

    std::cout << "Upgrading firmware...\n";
    buffer.seekg(0, std::ios::beg);
    if (firmwareSize > 0)
    {
        std::string sigStr;
        //read signatures
        if (DBB::GetArg("-noreadsig", "") == "")
        {
            unsigned char sigByte[FIRMWARE_SIGLEN];
            buffer.read((char *)&sigByte[0], FIRMWARE_SIGLEN);
            sigStr = DBB::HexStr(sigByte, sigByte + FIRMWARE_SIGLEN);
            printf("Reading signature... %s\n", sigStr.c_str());
        }

        //read firmware
        std::vector<char> firmwareBuffer(DBB_APP_LENGTH);
        unsigned int pos = 0;
        while (true)
        {
            buffer.read(&firmwareBuffer[0]+pos, FIRMWARE_CHUNKSIZE);
            std::streamsize bytes = buffer.gcount();
            if (bytes == 0)
                break;

            pos += bytes;
        }

        // append 0xff to the rest of the firmware buffer
        memset((void *)(&firmwareBuffer[0]+pos), 0xff, DBB_APP_LENGTH-pos);

        if (DBB::GetArg("-dummysigwrite", "") != "")
        {
            printf("Creating dummy signature...");
            btc_ecc_start();
            sigStr = DBB::dummySig(firmwareBuffer);
            printf("%s\n", sigStr.c_str());
            btc_ecc_stop();
        }

        // send firmware blob to DBB
        if (!DBB::upgradeFirmware(firmwareBuffer, firmwareSize, sigStr, [](const std::string& infotext, float progress) {
            std::cout << "Progress: " << round(progress*100) << "%\n";
        }))
            printf("Firmware upgrade failed!\n");
        else {

            std::cout << "Please unplug and replug the device. Don't press the touchbutton this time. Wait 5 seconds, then press return...\n";
            std::string dummyLine;
            std::getline(std::cin,dummyLine);
            std::string devicePath;
            enum DBB::dbb_device_mode deviceMode = DBB::deviceAvailable(devicePath);
            bool connectRes = DBB::openConnection(deviceMode, devicePath);
            bool sucFW = true;
            if (deviceMode == DBB::DBB_DEVICE_MODE_BOOTLOADER || deviceMode == DBB::DBB_DEVICE_NO_DEVICE || deviceMode == DBB::DBB_DEVICE_UNKNOWN)
            {
                std::cout << "Device is not in firmware mode. Please try again.\n";
                sucFW = false;
            }
            if (!connectRes) {
                printf("Error: No Digital Bitbox connected\n");
                sucFW = false;
            }
            if (sucFW) {
                std::cout << "Locking bootloader: please press touch button for more then 3 seconds.\n";
                std::string unlock = sendSingleCommand("{\"bootloader\" : \"lock\"}", password);
                if (unlock.find("\"code\":600") != std::string::npos) {
                    std::cout << "Aborted by user\n";
                }
                DBB::closeConnection();
            }

            printf("Firmware successfully upgraded!\n");
        }
    }
    else
        printf("Can't open firmware file!\n");

}

int main(int argc, char* argv[])
{
    DBB::ParseParameters(argc, argv);

    if (DBB::mapArgs.count("-help") || argc < 2) {
        printf("Usage: %s <password> (<firmware-file>)\n", "dbb_firmware");
        return 1;
    }

    std::string password = std::string(argv[1]);
    std::string possibleFilename = "";
    if (argc > 2 && argv[2][0] != '-')
        possibleFilename = std::string(argv[2]);

    std::string devicePath;
    enum DBB::dbb_device_mode deviceMode = DBB::deviceAvailable(devicePath);
    if (deviceMode == DBB::DBB_DEVICE_MODE_BOOTLOADER)
    {
        upgradeFirmware(password, possibleFilename);
        return 1;
    }
    else {
        bool connectRes = DBB::openConnection(deviceMode, devicePath);
        if (!connectRes) {
            printf("Error: No Digital Bitbox connected\n");
            return 1;
        }
        DebugOut("main", "Digital Bitbox connected\n");

        std::cout << "Unlocking bootloader: please press touch button for more then 3 seconds.\n";
        std::string unlock = sendSingleCommand("{\"bootloader\" : \"unlock\"}", password);
        DBB::closeConnection();
        if (unlock.find("\"code\":600") != std::string::npos) {
            std::cout << "Aborted by user\n";
            return 1;
        }

        std::cout << "Please unplug and re-plug the device, press the touch button after your have replugged the device\nPress enter after you have done this...";
        std::string dummyLine;
        std::getline(std::cin,dummyLine);
        // load the file
        enum DBB::dbb_device_mode deviceMode = DBB::deviceAvailable(devicePath);
        connectRes = DBB::openConnection(deviceMode, devicePath);
        if (deviceMode != DBB::DBB_DEVICE_MODE_BOOTLOADER)
        {
            std::cout << "Device is not in bootloader mode. Please try again.\n";
            DBB::closeConnection();
            return 1;
        }
        if (!connectRes) {
            printf("Error: No Digital Bitbox connected\n");
            return 1;
        }
        DBB::closeConnection();
        upgradeFirmware(password, possibleFilename);
    }

    return 0;
}
