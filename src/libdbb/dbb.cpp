// Copyright (c) 2015 Jonas Schnelli
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <assert.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <string>
#include <stdexcept>

#ifndef _SRC_CONFIG__DBB_CONFIG_H
#include "config/_dbb-config.h"
#endif

#include "dbb_util.h"
#include "crypto.h"

#include "univalue.h"
#include "hidapi/hidapi.h"

#define HID_REPORT_SIZE 2048

#ifdef DBB_ENABLE_DEBUG
#define DBB_DEBUG_INTERNAL(format, args...) printf(format, ##args);
#else
#define DBB_DEBUG_INTERNAL(format, args...)
#endif

namespace DBB
{
static hid_device* HID_HANDLE = NULL;
static unsigned char HID_REPORT[HID_REPORT_SIZE] = {0};

static bool api_hid_init(void)
{
    //TODO: way to handle multiple DBB
    HID_HANDLE = hid_open(0x03eb, 0x2402, NULL); //vendor-id, product-id
    if (!HID_HANDLE) {
        return false;
    }
    return true;
}

static bool api_hid_close(void)
{
    //TODO: way to handle multiple DBB
    if (HID_HANDLE) {
        hid_close(HID_HANDLE); //vendor-id, product-id
        return true;
    }

    return false;
}

bool isConnectionOpen()
{
    if (!HID_HANDLE)
        return false;

    struct hid_device_info* devs, *cur_dev;

    devs = hid_enumerate(0x03eb, 0x2402);
    cur_dev = devs;
    bool found = false;
    while (cur_dev) {
        found = true;
        break;
    }
    hid_free_enumeration(devs);

    return found;
}

bool openConnection()
{
    return api_hid_init();
}

bool closeConnection()
{
    return api_hid_close();
}

bool sendCommand(const std::string& json, std::string& resultOut)
{
    int res, cnt = 0;

    if (!HID_HANDLE)
        return false;

    DBB_DEBUG_INTERNAL("Sending command: %s\n", json.c_str());

    memset(HID_REPORT, 0, HID_REPORT_SIZE);
    memcpy(HID_REPORT, json.c_str(), json.size());
    hid_write(HID_HANDLE, (unsigned char*)HID_REPORT, HID_REPORT_SIZE);

    memset(HID_REPORT, 0, HID_REPORT_SIZE);
    DBB_DEBUG_INTERNAL("try to read some bytes...\n");

    memset(HID_REPORT, 0, HID_REPORT_SIZE);
    while (cnt < HID_REPORT_SIZE) {
        res = hid_read(HID_HANDLE, HID_REPORT + cnt, HID_REPORT_SIZE);
        if (res < 0) {
            throw std::runtime_error("Error: Unable to read HID(USB) report.\n");
        }
        cnt += res;
    }

    DBB_DEBUG_INTERNAL(" OK, read %d bytes.\n", res);

    resultOut.assign((const char*)HID_REPORT);
    return true;
}

bool decryptAndDecodeCommand(const std::string& cmdIn, const std::string& password, std::string& stringOut)
{
    unsigned char passwordSha256[DBB_SHA256_DIGEST_LENGTH];
    unsigned char aesIV[DBB_AES_BLOCKSIZE];
    unsigned char aesKey[DBB_AES_KEYSIZE];

    doubleSha256((char*)password.c_str(), passwordSha256);
    memcpy(aesKey, passwordSha256, DBB_AES_KEYSIZE);

    //decrypt result: TODO:
    UniValue valRead(UniValue::VSTR);
    if (!valRead.read(cmdIn))
        throw std::runtime_error("failed deserializing json");

    UniValue input = find_value(valRead, "input");
    if (!input.isNull() && input.isObject()) {
        UniValue error = find_value(input, "error");
        if (!error.isNull() && error.isStr())
            throw std::runtime_error("Error decrypting: " + error.get_str());
    }

    UniValue ctext = find_value(valRead, "ciphertext");
    if (ctext.isNull())
        throw std::runtime_error("failed deserializing json");

    std::string base64dec = base64_decode(ctext.get_str());
    unsigned int base64_len = base64dec.size();
    unsigned char* base64dec_c = (unsigned char*)base64dec.c_str();

    unsigned char* decryptedStream;
    unsigned char* decryptedCommand;
    memcpy(aesIV, base64dec_c, DBB_AES_BLOCKSIZE); //copy first 16 bytes and take as IV
    int outlen = 0;
    if (!aesDecrypt(aesKey, aesIV, base64dec_c + DBB_AES_BLOCKSIZE, base64_len - DBB_AES_BLOCKSIZE, &decryptedStream, &outlen))
        throw std::runtime_error("decryption failed");

    int decrypt_len = 0;
    int padlen = decryptedStream[base64_len - DBB_AES_BLOCKSIZE - 1];
    char* dec = (char*)malloc(base64_len - DBB_AES_BLOCKSIZE - padlen + 1); // +1 for null termination
    if (!dec) {
        decrypt_len = 0;
        memset(decryptedStream, 0, sizeof(&decryptedStream));
        free(decryptedStream);
        throw std::runtime_error("decription failed");
        return false;
    }
    memcpy(dec, decryptedStream, base64_len - DBB_AES_BLOCKSIZE - padlen);
    dec[base64_len - DBB_AES_BLOCKSIZE - padlen] = 0;
    decrypt_len = base64_len - DBB_AES_BLOCKSIZE - padlen + 1;
    memset(decryptedStream, 0, sizeof(&decryptedStream));
    free(decryptedStream);

    stringOut.assign((const char*)dec);
    free(dec);
    return true;
}

bool encryptAndEncodeCommand(const std::string& cmd, const std::string& password, std::string& base64strOut)
{
    if (password.empty())
        return false;

    //double sha256 the password
    unsigned char passwordSha256[DBB_SHA256_DIGEST_LENGTH];
    unsigned char* cypher;
    unsigned char aesIV[DBB_AES_BLOCKSIZE];
    unsigned char aesKey[DBB_AES_KEYSIZE];

    doubleSha256((char*)password.c_str(), passwordSha256);

    //set random IV
    getRandIV(aesIV);
    memcpy(aesKey, passwordSha256, DBB_AES_KEYSIZE);

    int inlen = cmd.size();
    unsigned int pads = 0;
    int inpadlen = inlen + DBB_AES_BLOCKSIZE - inlen % DBB_AES_BLOCKSIZE;
    unsigned char inpad[inpadlen];
    unsigned char enc[inpadlen];
    unsigned char enc_cat[inpadlen + DBB_AES_BLOCKSIZE]; // concatenating [ iv0  |  enc ]

    // PKCS7 padding
    memcpy(inpad, cmd.c_str(), inlen);
    for (pads = 0; pads < DBB_AES_BLOCKSIZE - inlen % DBB_AES_BLOCKSIZE; pads++) {
        inpad[inlen + pads] = (DBB_AES_BLOCKSIZE - inlen % DBB_AES_BLOCKSIZE);
    }

    //add iv to the stream for base64 encoding
    memcpy(enc_cat, aesIV, DBB_AES_BLOCKSIZE);

    //encrypt
    aesEncrypt(aesKey, aesIV, inpad, inlen, &cypher);

    //copy the encypted data to the stream where the iv is already
    memcpy(enc_cat + DBB_AES_BLOCKSIZE, cypher, inpadlen);
    free(cypher);

    //base64 encode
    base64strOut = base64_encode(enc_cat, inpadlen + DBB_AES_BLOCKSIZE);

    return true;
}
}
