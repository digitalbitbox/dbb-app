// Copyright (c) 2015 Jonas Schnelli
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bpwalletclient.h"

#include "config/_dbb-config.h"

#if defined _MSC_VER
#include <direct.h>
#elif defined __GNUC__
#include <sys/types.h>
#include <sys/stat.h>
#endif

#include <algorithm>
#include <assert.h>
#include <ctime>
#include <string.h>

#include "libdbb/crypto.h"
#include "dbb_util.h"

#include <btc/base58.h>
#include <btc/ecc_key.h>
#include <btc/ecc.h>
#include <btc/hash.h>
#include <btc/bip32.h>
#include <btc/tx.h>

#if defined WIN32
#include <shlobj.h>
#endif

//ignore osx depracation warning
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

#include <climits>

#ifdef DBB_ENABLE_DEBUG
#define BP_LOG_MSG printf
#else
#define BP_LOG_MSG(f_, ...)
#endif

std::string BitPayWalletClient::ReversePairs(std::string const& src)
{
    assert(src.size() % 2 == 0);
    std::string result;
    result.reserve(src.size());

    for (std::size_t i = src.size(); i != 0; i -= 2) {
        result.append(src, i - 2, 2);
    }

    return result;
}

BitPayWalletClient::BitPayWalletClient(std::string dataDirIn, bool testnetIn) : dataDir(dataDirIn), testnet(testnetIn), walletJoined(false)
{
    //set the default wallet service
    ca_file = "";
    baseURL = "https://bws.bitpay.com/bws/api";
    filenameBase.clear();
    socks5ProxyURL.clear();
}

void BitPayWalletClient::setBaseURL(const std::string& baseURLnew)
{
    std::unique_lock<std::recursive_mutex> lock(this->cs_client);
    baseURL = baseURLnew;
}

void BitPayWalletClient::setFilenameBase(const std::string& filenameBaseIn)
{
    if (filenameBaseIn.size() > 0)
        filenameBase = filenameBaseIn;
}

const std::string& BitPayWalletClient::getFilenameBase()
{
    return filenameBase;
}

BitPayWalletClient::~BitPayWalletClient()
{
}

//helper: split a string into chunks (re-wrote in c++ from copay [js])
std::vector<std::string> BitPayWalletClient::split(const std::string& str, std::vector<int> indexes)
{
    std::vector<std::string> parts;
    indexes.push_back(str.size());
    int i = 0;
    while (i < indexes.size()) {
        int from = i == 0 ? 0 : indexes[i - 1];
        parts.push_back(str.substr(from, indexes[i] - from));
        i++;
    };
    return parts;
};

std::string BitPayWalletClient::_copayerHash(const std::string& name, const std::string& xPubKey, const std::string& requestPubKey)
{
    return name + "|" + xPubKey + "|" + requestPubKey;
};

std::string BitPayWalletClient::GetXPubKey()
{
    return masterPubKey;
}

bool BitPayWalletClient::GetCopayerHash(const std::string& name, std::string& out)
{
    std::unique_lock<std::recursive_mutex> lock(this->cs_client);

    std::string requestKeyHex;
    if (!GetRequestPubKey(requestKeyHex))
        return false;

    out = _copayerHash(name, masterPubKey, requestKeyHex);
    return true;
};

//wrapper for a double sha256
void BitPayWalletClient::Hash(const std::string& stringIn, uint8_t* hashout)
{
    const char* s = stringIn.c_str();
    btc_hash((const uint8_t*)s, stringIn.size(), hashout);
}

// creates a hex signature for the given string
bool BitPayWalletClient::GetCopayerSignature(const std::string& stringToHash, const uint8_t* privKey, std::string& sigHexOut)
{
    bool success = false;
    uint8_t hash[32];
    Hash(stringToHash, hash);

    btc_key key;
    btc_privkey_init(&key);
    memcpy(&key.privkey, privKey, 32);
    btc_pubkey pubkey;
    btc_pubkey_init(&pubkey);
    btc_pubkey_from_key(&key, &pubkey);

    unsigned char sig[74];
    size_t outlen = 74;
    memset(sig, 0, 74);
    uint8_t hash2[32];
    memcpy(hash2, hash, 32);

    btc_key_sign_hash(&key, hash, sig, &outlen);

    if (btc_pubkey_verify_sig(&pubkey, hash2, sig, outlen) == 1) {
        std::vector<unsigned char> signature;
        signature.assign(sig, sig + outlen);
        sigHexOut = DBB::HexStr(sig, sig + outlen);
        success = true;
    } else {
        success = false;
    }

    btc_privkey_cleanse(&key);
    btc_pubkey_cleanse(&pubkey);
    return success;
};

//set the extended master pub key
void BitPayWalletClient::setMasterPubKey(const std::string& xPubKey)
{
    std::unique_lock<std::recursive_mutex> lock(this->cs_client);

    masterPubKey = xPubKey;
    SaveLocalData();
}

void BitPayWalletClient::setRequestPubKey(const std::string& xPubKeyRequestKeyEntropy)
{
    //now this is a ugly workaround because we need a request keypair (pub/priv)
    //for signing the requests after BitAuth
    //Signing over the hardware wallet would be a very bad UX (press button on
    // every request) and it would be slow
    //the request key should be deterministic and linked to the master key
    //
    //we now generate a private key by (miss)using the xpub at m/1'/0' as entropy
    //for a new private key

    std::unique_lock<std::recursive_mutex> lock(this->cs_client);

    btc_hdnode node;
    bool r = btc_hdnode_deserialize(xPubKeyRequestKeyEntropy.c_str(), (testnet ? &btc_chain_test : &btc_chain_main), &node);

    memcpy(requestKey.privkey, node.public_key + 1, 32);
    std::vector<unsigned char> hash = DBB::ParseHex("26db47a48a10b9b0b697b793f5c0231aa35fe192c9d063d7b03a55e3c302850a");

    unsigned char sig[74];
    size_t outlen = 74;
    assert(btc_key_sign_hash(&requestKey, &hash.front(), sig, &outlen) == 1);


    btc_pubkey pubkey;
    btc_pubkey_init(&pubkey);
    btc_pubkey_from_key(&requestKey, &pubkey);

    unsigned int i;
    for (i = 33; i < BTC_ECKEY_UNCOMPRESSED_LENGTH; i++)
        assert(pubkey.pubkey[i] == 0);

    assert(btc_pubkey_verify_sig(&pubkey, &hash.front(), sig, outlen) == 1);
    btc_pubkey_cleanse(&pubkey);

    SaveLocalData();
}

//returns the request pubkey
//TODO: requires caching
bool BitPayWalletClient::GetRequestPubKey(std::string& pubKeyOut)
{
    std::unique_lock<std::recursive_mutex> lock(this->cs_client);

    btc_pubkey pubkey;
    btc_pubkey_init(&pubkey);
    btc_pubkey_from_key(&requestKey, &pubkey);

    pubKeyOut = DBB::HexStr(pubkey.pubkey, pubkey.pubkey + 33);

    btc_pubkey_cleanse(&pubkey);

    return true;
}

//!returns to copyer ID (=single sha256 of the masterpubkey)
std::string BitPayWalletClient::GetCopayerId()
{
    std::unique_lock<std::recursive_mutex> lock(this->cs_client);

    uint8_t hashout[32];
    //here we need a signle sha256
    btc_hash_sngl_sha256((const uint8_t*)masterPubKey.c_str(), masterPubKey.size(), hashout);
    return DBB::HexStr(hashout, hashout + 32);
}

bool BitPayWalletClient::ParseWalletInvitation(const std::string& walletInvitation, BitpayWalletInvitation& invitationOut)
{
    if (walletInvitation.size() < 74)
        return false;

    std::vector<int> splits = {22, 74};
    std::vector<std::string> secretSplit = split(walletInvitation, splits);

    std::string widBase58 = secretSplit[0];
    std::vector<unsigned char> vch;

    size_t buflen = widBase58.size();
    uint8_t buf[buflen * 3];
    if (!btc_base58_decode(buf, &buflen, widBase58.c_str()))
        return false;

    std::string widHex = DBB::HexStr(buf + 6, buf + 6 + buflen, false);

    splits = {8, 12, 16, 20};
    std::vector<std::string> walletIdParts = split(widHex, splits);
    invitationOut.walletID = walletIdParts[0] + "-" + walletIdParts[1] + "-" + walletIdParts[2] + "-" + walletIdParts[3] + "-" + walletIdParts[4];


    std::string walletPrivKeyStr = secretSplit[1];
    uint8_t rawn[walletPrivKeyStr.size()];
    if (!btc_base58_decode_check(walletPrivKeyStr.c_str(), rawn, 100))
        return false;


    memcpy(invitationOut.walletPrivKey, &rawn[1], 32);
    invitationOut.network = secretSplit[2] == "T" ? "testnet" : "livenet";
    return true;
}

bool BitPayWalletClient::GetNewAddress(std::string& newAddress,std::string& keypath, std::string &error)
{
    //form request
    UniValue jsonArgs(UniValue::VOBJ);
    std::string json = jsonArgs.write();

    long httpStatusCode = 0;
    std::string response;
    if (!SendRequest("post", "/v3/addresses/", json, response, httpStatusCode))
        return false;

    if (httpStatusCode != 200) {
        UniValue errorUni;
        errorUni.read(response);
        if (errorUni.isObject()) {
            UniValue messageUni = find_value(errorUni, "message");
            if (messageUni.isStr()) {
                error = messageUni.get_str();
            }
        }
        DBB::LogPrint("GetNewAddress non 200 http status code (got %d)\n", httpStatusCode);
        return false;
    }

    UniValue responseUni;
    responseUni.read(response);

    if (!responseUni.isObject())
        return false;

    UniValue addressUV = find_value(responseUni, "address");
    if (!addressUV.isStr())
        return false;

    UniValue pathUV = find_value(responseUni, "path");
    if (pathUV.isStr())
        keypath = pathUV.get_str();

    newAddress = addressUV.get_str();
    {
        std::unique_lock<std::recursive_mutex> lock(this->cs_client);
        lastKnownAddressJson = response;
        SaveLocalData();
    }

    return true;
}

bool BitPayWalletClient::GetLastKnownAddress(std::string& address, std::string& keypath)
{
    std::unique_lock<std::recursive_mutex> lock(this->cs_client);
    
    if (lastKnownAddressJson.size() == 0)
        return false;

    UniValue responseUni;
    responseUni.read(lastKnownAddressJson);

    if (!responseUni.isObject())
        return false;

    UniValue addressUV = find_value(responseUni, "address");
    if (!addressUV.isStr())
        return false;

    address = addressUV.get_str();

    UniValue keypathUV = find_value(responseUni, "path");
    if (keypathUV.isStr())
        keypath = keypathUV.get_str();

    return true;
}


bool BitPayWalletClient::CreatePaymentProposal(const std::string& address, uint64_t amount, uint64_t feeperkb, UniValue& paymentProposalOut, std::string& errorOut)
{
    //form request
    UniValue outputs(UniValue::VARR);

    //currently we only support a single output
    UniValue mainOutput(UniValue::VOBJ);

    //add the output
    mainOutput.push_back(Pair("toAddress", address));
    mainOutput.push_back(Pair("amount", amount));
    mainOutput.push_back(Pair("message", ""));
    mainOutput.push_back(Pair("script", ""));
    outputs.push_back(mainOutput);

    UniValue jsonArgs(UniValue::VOBJ);
    jsonArgs.push_back(Pair("feePerKb", feeperkb));
    jsonArgs.push_back(Pair("payProUrl", false));
    jsonArgs.push_back(Pair("type", "simple"));
    jsonArgs.push_back(Pair("version", "1.0.0"));
    jsonArgs.push_back(Pair("outputs", outputs));
    std::string json = jsonArgs.write();

    long httpStatusCode = 0;
    std::string response;
    if (!SendRequest("post", "/v2/txproposals/", json, response, httpStatusCode))
    {
        errorOut = "Connection failed";
        return false;
    }

    if (httpStatusCode == 400) {
        UniValue responseUni;
        responseUni.read(response);

        UniValue codeUni;
        codeUni = find_value(responseUni, "code");
        if (codeUni.isStr() && codeUni.get_str() == "LOCKED_FUNDS") {
            //try to unlock funds
            std::string walletResponse;
            if (!GetWallets(walletResponse)) {
                errorOut = "Could not unlock funds";
                return false;
            }

            UniValue walletResponseUni;
            walletResponseUni.read(walletResponse);

            UniValue pendingTxps;
            pendingTxps = find_value(walletResponseUni, "pendingTxps");
            if (!pendingTxps.isNull() && pendingTxps.isArray()) {
                std::vector<UniValue> values = pendingTxps.getValues();
                for (const UniValue& oneProposal : values) {
                    DeleteTxProposal(oneProposal);
                }
            }

            //post again
            response.clear();
            httpStatusCode = 0;
            int reqRet = SendRequest("post", "/v2/txproposals/", json, response, httpStatusCode);
            if (!reqRet || httpStatusCode != 200) {
                errorOut = "Could not unlock funds";
                DBB::LogPrint("Could not unlock funds\n", httpStatusCode);
                return false;
            }
            paymentProposalOut.read(response);

            if (!PublishTxProposal(paymentProposalOut, errorOut)) {
                DBB::LogPrint("Publish tx proposal failed (%s)\n", errorOut.c_str());
                return false;
            }
            
            return true;
        } else {
            //unknown error
            DBB::LogPrint("Uknown error during CreatePaymentProposal\n");
            UniValue messageUni;
            messageUni = find_value(responseUni, "message");

            if (messageUni.isStr())
                errorOut = messageUni.get_str();
            return false;
        }
    }

    paymentProposalOut.read(response);
    if (!paymentProposalOut.isObject())
        return false;

    if (!PublishTxProposal(paymentProposalOut, errorOut))
        return false;

    return true;
}


bool BitPayWalletClient::PublishTxProposal(const UniValue& paymentProposal, std::string& errorOut)
{
    // get txpid
    UniValue pID = find_value(paymentProposal, "id");
    std::string txpID = "";
    if (pID.isStr())
        txpID = pID.get_str();

    // get serialized tx hex
    std::vector<std::pair<std::string, std::vector<unsigned char> > > inputHashesAndPaths;
    std::string serTx;
    UniValue changeAddressData;
    ParseTxProposal(paymentProposal, changeAddressData, serTx, inputHashesAndPaths, true);

    // sign the hex with the copay request key
    std::string txHashSig;
    GetCopayerSignature(serTx, requestKey.privkey, txHashSig);

    UniValue signatureJson(UniValue::VOBJ);
    signatureJson.push_back(Pair("proposalSignature", txHashSig));


    long httpStatusCode = 0;
    std::string response;

    if (!SendRequest("post", "/v1/txproposals/"+txpID+"/publish/", signatureJson.write(), response, httpStatusCode))
    {
        errorOut = "Connection failed";
        DBB::LogPrint("Connection failed during PublishTxProposal\n");
        return false;
    }

    if (httpStatusCode != 200) {
        errorOut = "Could not publish transaction proposal";
        DBB::LogPrint("Connection failed during PublishTxProposal (got http status %d)\n", httpStatusCode);
        return false;
    }

    return true;
}

bool BitPayWalletClient::CreateWallet(const std::string& walletName)
{
    btc_key key;
    btc_privkey_init(&key);
    btc_privkey_gen(&key);

    btc_pubkey pubkey;
    btc_pubkey_init(&pubkey);
    btc_pubkey_from_key(&key, &pubkey);

    std::string pubKeyHex = DBB::HexStr(pubkey.pubkey, pubkey.pubkey + 33);

    //form request
    UniValue jsonArgs(UniValue::VOBJ);
    jsonArgs.push_back(Pair("m", 1));
    jsonArgs.push_back(Pair("n", 1));
    jsonArgs.push_back(Pair("name", walletName));
    jsonArgs.push_back(Pair("pubKey", pubKeyHex));
    jsonArgs.push_back(Pair("network", (testnet ? "testnet" : "livenet")));
    std::string json = jsonArgs.write();

    long httpStatusCode = 0;
    std::string response;
    if (!SendRequest("post", "/v2/wallets/", json, response, httpStatusCode))
        return false;

    if (httpStatusCode != 200) {
        DBB::LogPrint("Got non 200 http status during CreateWallet (got %d)\n", httpStatusCode);
        return false;
    }

    UniValue responseUni;
    responseUni.read(response);

    if (!responseUni.isObject())
        return false;

    UniValue walletID = find_value(responseUni, "walletId");
    if (!walletID.isStr())
        return false;


    BitpayWalletInvitation inv;
    inv.walletID = walletID.get_str();
    inv.network = "testnet";
    memcpy(inv.walletPrivKey, key.privkey, 32);

    std::string newResponse;
    JoinWallet("digitalbitbox", inv, newResponse);

    return true;
}

bool BitPayWalletClient::JoinWallet(const std::string& name, const BitpayWalletInvitation invitation, std::string& response)
{
    std::string requestPubKey;
    if (!GetRequestPubKey(requestPubKey))
        return false;

    std::string copayerHash;
    if (!GetCopayerHash(name, copayerHash))
        return false;

    std::string copayerSignature;
    if (!GetCopayerSignature(copayerHash, invitation.walletPrivKey, copayerSignature))
        return false;

    //form request
    UniValue jsonArgs(UniValue::VOBJ);
    jsonArgs.push_back(Pair("walletId", invitation.walletID));
    jsonArgs.push_back(Pair("name", name));
    jsonArgs.push_back(Pair("xPubKey", GetXPubKey()));
    jsonArgs.push_back(Pair("requestPubKey", requestPubKey));
    jsonArgs.push_back(Pair("isTemporaryRequestKey", false));
    jsonArgs.push_back(Pair("copayerSignature", copayerSignature));
    std::string json = jsonArgs.write();

    long httpStatusCode = 0;
    if (!SendRequest("post", "/v2/wallets/" + invitation.walletID + "/copayers", json, response, httpStatusCode))
        return false;

    std::string getWalletsResponse;
    GetWallets(getWalletsResponse);

    if (httpStatusCode != 200)
        return false;

    return true;
}

bool BitPayWalletClient::GetFeeLevels()
{
    std::string requestPubKey;
    std::string response;
    if (!GetRequestPubKey(requestPubKey))
        return false;

    long httpStatusCode = 0;
    if (SendRequest("get", "https://www.digitalbitbox.com/fees/levels.json?r="+std::to_string(CheapRandom()), "{}", response, httpStatusCode, false)) {
        // we could read our "internal" fees
        feeLevelsObject.read(response);
        if (response.size() > 10 && feeLevelsObject.isArray()) {
            return true;
        }
    }

    response.clear();
    httpStatusCode = 0;
    if (!SendRequest("get", "/v1/feelevels/?network=livenet&r="+std::to_string(CheapRandom()), "{}", response, httpStatusCode))
        return false;

    if (httpStatusCode != 200)
        return false;

    feeLevelsObject.read(response);
    return true;
}

int64_t BitPayWalletClient::GetFeeForPriority(int prio)
{
    std::unique_lock<std::recursive_mutex> lock(this->cs_client);
    
    std::string keyField = "";
    if (prio == 1)
        keyField = "normal";
    else if (prio == 2)
        keyField = "economy";
    else if (prio == 3)
        keyField = "superEconomy";
    else
        keyField = "priority";

    if (feeLevelsObject.isArray())
    {
        std::vector<UniValue> values = feeLevelsObject.getValues();
        for (const UniValue& oneObj : values)
        {
            UniValue levelUV = find_value(oneObj, "level");
            UniValue feePerKBUB = find_value(oneObj, "feePerKB");
            if (levelUV.isStr() && levelUV.get_str() == keyField)
            {
                return feePerKBUB.get_int64();
            }
        }
    }

    return 0; //don't use a fallback fee, abort at this point
}

bool BitPayWalletClient::GetWallets(std::string& response)
{
    std::string requestPubKey;
    if (!GetRequestPubKey(requestPubKey))
        return false;

    long httpStatusCode = 0;
    if (!SendRequest("get", "/v2/wallets/?r="+std::to_string(CheapRandom()), "{}", response, httpStatusCode))
        return false;

    if (httpStatusCode != 200)
        return false;

    return true;
}

bool BitPayWalletClient::GetTransactionHistory(std::string& response)
{
    std::string requestPubKey;
    if (!GetRequestPubKey(requestPubKey))
        return false;

    long httpStatusCode = 0;
    if (!SendRequest("get", "/v1/txhistory/?r="+std::to_string(CheapRandom()), "{}", response, httpStatusCode))
        return false;

    if (httpStatusCode != 200)
        return false;

    BP_LOG_MSG("Response: %s\n", response.c_str());
    return true;
}

void BitPayWalletClient::ParseTxProposal(const UniValue& txProposal, UniValue& changeAddressData, std::string& serTx, std::vector<std::pair<std::string, std::vector<unsigned char> > >& vInputTxHashes, bool noScriptPubKey)
{
    btc_tx* tx = btc_tx_new();

    if (!txProposal.isObject())
        return;

    std::vector<std::string> keys = txProposal.getKeys();
    std::vector<UniValue> values = txProposal.getValues();

    std::string toAddress;
    int64_t toAmount = -1;
    int64_t fee = -1;
    std::vector<int> outputOrder;
    int requiredSignatures = -1;
    int i = 0;
    int j = 0;
    int64_t inTotal = 0;
    std::vector<std::pair<std::string, std::vector<unsigned char> > > inputsScriptAndPath;

    for (i = 0; i < keys.size(); i++) {
        UniValue val = values[i];

        if (keys[i] == "outputs")
        {
            UniValue firstOutput = val[0];
            UniValue addressObj = find_value(firstOutput, "toAddress");
            UniValue toAmountObj = find_value(firstOutput, "amount");
            if (addressObj.isStr())
                toAddress = addressObj.get_str();
            if (toAmountObj.isNum())
                toAmount = toAmountObj.get_int64();
        }

        if (keys[i] == "fee")
            fee = val.get_int64();

        if (keys[i] == "outputOrder")
            for (UniValue aVal : val.getValues())
                outputOrder.push_back(aVal.get_int());

        if (keys[i] == "requiredSignatures")
            requiredSignatures = val.get_int();
    }

    UniValue inputsObj = find_value(txProposal, "inputs");
    std::vector<UniValue> inputs = inputsObj.getValues();

    UniValue addressTypeUni = find_value(txProposal, "addressType");

    for (i = 0; i < inputs.size(); i++) {

        UniValue aInput = inputs[i];
        std::vector<std::string> keys = aInput.getKeys();
        std::vector<UniValue> values = aInput.getValues();

        std::string txId;
        std::vector<std::string> publicKeys;
        std::string path;
        int nInput = -1;

        for (j = 0; j < keys.size(); j++) {
            UniValue val = values[j];
            if (keys[j] == "txid")
                txId = val.get_str();

            if (keys[j] == "vout")
                nInput = val.get_int();

            if (keys[j] == "satoshis")
                inTotal += val.get_int64();

            if (keys[j] == "path")
                path = val.get_str();

            if (keys[j] == "publicKeys") {
                std::vector<UniValue> pubKeyValue = val.getValues();
                int k;
                for (k = 0; k < pubKeyValue.size(); k++) {
                    UniValue aPubKeyObj = pubKeyValue[k];
                    publicKeys.push_back(aPubKeyObj.get_str());
                }

                //sort keys
                std::sort(publicKeys.begin(), publicKeys.end());
            }
        }

        // reverse txid and parse hex
        std::vector<unsigned char> aHash = DBB::ParseHex(ReversePairs(txId));

        // add the input to the tx
        btc_tx_in* txin = btc_tx_in_new();
        memcpy(txin->prevout.hash, &aHash[0], 32);
        txin->prevout.n = nInput;

        vector* v_pubkeys = vector_new(3, free);
        int k;
        for (k = 0; k < publicKeys.size(); k++) {
            btc_pubkey* pubkey = (btc_pubkey*)malloc(sizeof(btc_pubkey));
            btc_pubkey_init(pubkey);
            std::vector<unsigned char> data = DBB::ParseHex(publicKeys[k]);

            //TODO: allow uncompressed keys
            pubkey->compressed = true;
            memcpy(pubkey->pubkey, &data[0], 33);
            vector_add(v_pubkeys, pubkey);
        }

        if (addressTypeUni.isStr() && addressTypeUni.get_str() == "P2PKH")
        {
            cstring* script = cstr_new_sz(1024); //create P2PKH
            btc_script_append_op(script, OP_DUP);
            btc_script_append_op(script, OP_HASH160);

            if (v_pubkeys->len == 1)
            {
                btc_pubkey* pubkey = (btc_pubkey *)vector_idx(v_pubkeys, 0);
                uint8_t hash160[20];
                btc_pubkey_get_hash160(pubkey, hash160);
                btc_script_append_pushdata(script, (unsigned char*)hash160, 20);
            }

            btc_script_append_op(script, OP_EQUALVERIFY);
            btc_script_append_op(script, OP_CHECKSIG);

            std::vector<unsigned char> vScript(script->len);
            vScript.assign(script->str, script->str + script->len);
            path.erase(0, 2); //remove m/ from path
            inputsScriptAndPath.push_back(std::make_pair(path, vScript));

            txin->script_sig = cstr_new_sz(script->len);
            if (!noScriptPubKey)
                cstr_append_buf(txin->script_sig, script->str, script->len);
            
            vector_add(tx->vin, txin);
            cstr_free(script, true);
        }
        else
        {
            //assume P2SH / n-of-m

            cstring* script = cstr_new_sz(1024); //create P2SH, MS
            btc_script_append_op(script, OP_0);  //multisig workaround

            cstring* msscript = cstr_new_sz(1024); //create multisig script
            btc_script_build_multisig(msscript, requiredSignatures, v_pubkeys);

            // append script for P2SH / OP_0
            btc_script_append_pushdata(script, (unsigned char*)msscript->str, msscript->len);

            // store the script for later sighash operations
            std::vector<unsigned char> msP2SHScript(msscript->len);
            msP2SHScript.assign(msscript->str, msscript->str + msscript->len);
            cstr_free(msscript, true);
            path.erase(0, 2); //remove m/ from path
            inputsScriptAndPath.push_back(std::make_pair(path, msP2SHScript));

            // reverse txid and parse hex
            std::vector<unsigned char> aHash = DBB::ParseHex(ReversePairs(txId));

            // add the input to the tx
            btc_tx_in* txin = btc_tx_in_new();
            memcpy(txin->prevout.hash, &aHash[0], 32);
            txin->prevout.n = nInput;
            txin->script_sig = cstr_new_sz(script->len);
            if (!noScriptPubKey)
                cstr_append_buf(txin->script_sig, script->str, script->len);
            vector_add(tx->vin, txin);

            cstr_free(script, true);
        }

        //free pubkey vector
        vector_free(v_pubkeys, true);
    }

    // find out change address
    changeAddressData = find_value(txProposal, "changeAddress");
    keys = changeAddressData.getKeys();
    values = changeAddressData.getValues();
    std::string changeAdr = "";
    for (i = 0; i < keys.size(); i++) {
        UniValue val = values[i];

        if (keys[i] == "address")
            changeAdr = val.get_str();
    }

    int64_t changeAmount = inTotal - toAmount - fee;

    if (changeAmount == 0)
    {
        // don't add a change address when there the changeAmount if 0
        btc_tx_add_address_out(tx, (testnet ? &btc_chain_test : &btc_chain_main), toAmount, toAddress.c_str());
    }
    else
    {
        // flip output order after value given by the wallet server
        if (outputOrder.size() > 0 && outputOrder[0] == 1) {
            btc_tx_add_address_out(tx, (testnet ? &btc_chain_test : &btc_chain_main), changeAmount, changeAdr.c_str());
            btc_tx_add_address_out(tx, (testnet ? &btc_chain_test : &btc_chain_main), toAmount, toAddress.c_str());
        } else {
            btc_tx_add_address_out(tx, (testnet ? &btc_chain_test : &btc_chain_main), toAmount, toAddress.c_str());
            btc_tx_add_address_out(tx, (testnet ? &btc_chain_test : &btc_chain_main), changeAmount, changeAdr.c_str());
        }
    }

    cstring* txser = cstr_new_sz(1024);
    btc_tx_serialize(txser, tx);
    serTx = DBB::HexStr((unsigned char*)txser->str, (unsigned char*)txser->str + txser->len);
    BP_LOG_MSG("\n\nhextx: %s\n\n", serTx.c_str());
    cstr_free(txser, true);
    int cnt = 0;

    for (cnt = 0; cnt < tx->vin->len; cnt++) {
        std::pair<std::string, std::vector<unsigned char> > scriptAndPath = inputsScriptAndPath[cnt];
        std::vector<unsigned char> aScript = scriptAndPath.second;
        std::string scriptHex = DBB::HexStr((unsigned char*)&aScript[0],(unsigned char*)&aScript.back()+1);
        BP_LOG_MSG("\n\nscripthex for %d: %s\n\n", cnt, scriptHex.c_str());

        cstring* new_script = cstr_new_buf(&aScript[0], aScript.size());
        uint8_t hash[32];
        btc_tx_sighash(tx, new_script, cnt, 1, hash);
        cstr_free(new_script, true);
        std::string sSigDER2 = DBB::HexStr((unsigned char*)hash, (unsigned char*)hash + 32);

        std::vector<unsigned char> vHash(32);
        vHash.assign(hash, hash + 32);
        vInputTxHashes.push_back(std::make_pair(scriptAndPath.first, vHash));
    }

    btc_tx_free(tx);
}

int ecdsa_sig_to_der(const uint8_t* sig, uint8_t* der)
{
    int i;
    uint8_t *p = der, *len, *len1, *len2;
    *p = 0x30;
    p++; // sequence
    *p = 0x00;
    len = p;
    p++; // len(sequence)

    *p = 0x02;
    p++; // integer
    *p = 0x00;
    len1 = p;
    p++; // len(integer)

    // process R
    i = 0;
    while (sig[i] == 0 && i < 32) {
        i++;
    }                     // skip leading zeroes
    if (sig[i] >= 0x80) { // put zero in output if MSB set
        *p = 0x00;
        p++;
        *len1 = *len1 + 1;
    }
    while (i < 32) { // copy bytes to output
        *p = sig[i];
        p++;
        *len1 = *len1 + 1;
        i++;
    }

    *p = 0x02;
    p++; // integer
    *p = 0x00;
    len2 = p;
    p++; // len(integer)

    // process S
    i = 32;
    while (sig[i] == 0 && i < 64) {
        i++;
    }                     // skip leading zeroes
    if (sig[i] >= 0x80) { // put zero in output if MSB set
        *p = 0x00;
        p++;
        *len2 = *len2 + 1;
    }
    while (i < 64) { // copy bytes to output
        *p = sig[i];
        p++;
        *len2 = *len2 + 1;
        i++;
    }

    *len = *len1 + *len2 + 4;
    return *len + 2;
}

bool BitPayWalletClient::RejectTxProposal(const UniValue& txProposal)
{
    //parse out the txpid
    UniValue pID = find_value(txProposal, "id");
    std::string txpID = "";
    if (pID.isStr())
        txpID = pID.get_str();

    UniValue rejectRequest = UniValue(UniValue::VOBJ);
    rejectRequest.push_back(Pair("reason", ""));
    std::string response;
    long httpStatusCode = 0;
    if (!SendRequest("post", "/v1/txproposals/" + txpID + "/rejections/", rejectRequest.write(), response, httpStatusCode) ||httpStatusCode != 200)
        return false;

    return true;
}

bool BitPayWalletClient::DeleteTxProposal(const UniValue& txProposal)
{
    //parse out the txpid
    UniValue pID = find_value(txProposal, "id");
    std::string txpID = "";
    if (pID.isStr())
        txpID = pID.get_str();

    UniValue rejectRequest = UniValue(UniValue::VOBJ);
    std::string response;
    long httpStatusCode = 0;
    if (!SendRequest("delete", "/v1/txproposals/" + txpID, rejectRequest.write(), response, httpStatusCode) || httpStatusCode != 200)
        return false;

    return true;
}

bool BitPayWalletClient::PostSignaturesForTxProposal(const UniValue& txProposal, const std::vector<std::string>& vHexSigs)
{
    //parse out the txpid
    UniValue pID = find_value(txProposal, "id");
    std::string txpID = "";
    if (pID.isStr())
        txpID = pID.get_str();

    UniValue signaturesRequest = UniValue(UniValue::VOBJ);
    UniValue sigs = UniValue(UniValue::VARR);
    for (const std::string& sSig : vHexSigs) {
        std::vector<unsigned char> data = DBB::ParseHex(sSig);
        size_t sigder_len = 74;
        unsigned char sigder[sigder_len];
        btc_ecc_compact_to_der_normalized(&data[0], sigder, &sigder_len);
        std::string sSigDER = DBB::HexStr(sigder, sigder + sigder_len);
        sigs.push_back(sSigDER);
    }
    signaturesRequest.push_back(Pair("signatures", sigs));
    std::string response;
    long httpStatusCode = 0;
    if (!SendRequest("post", "/v1/txproposals/" + txpID + "/signatures/", signaturesRequest.write(), response, httpStatusCode) || httpStatusCode != 200)
        return false;

    return true;
}

bool BitPayWalletClient::BroadcastProposal(const UniValue& txProposal)
{
    std::string requestPubKey;
    if (!GetRequestPubKey(requestPubKey))
        return false;

    UniValue pID = find_value(txProposal, "id");
    std::string txpID = "";
    if (pID.isStr())
        txpID = pID.get_str();

    std::string response;
    long httpStatusCode = 0;
    if (!SendRequest("post", "/v1/txproposals/" + txpID + "/broadcast/", "{}", response, httpStatusCode))
        return false;

    BP_LOG_MSG("Response: %s\n", response.c_str());
    if (httpStatusCode != 200)
        return false;

    return true;
}


std::string BitPayWalletClient::SignRequest(const std::string& method,
                                            const std::string& url,
                                            const std::string& args,
                                            std::string& hashOut)
{
    std::unique_lock<std::recursive_mutex> lock(this->cs_client);

    std::string message = method + "|" + url + "|" + args;
    uint8_t hash[32];
    btc_hash((const unsigned char*)&message.front(), message.size(), hash);

    BP_LOG_MSG("signing message: %s, hash: %s\n", message.c_str(), DBB::HexStr(hash, hash + 32).c_str());

    unsigned char sig[74];
    size_t outlen = 74;
    btc_key_sign_hash(&requestKey, hash, sig, &outlen);

    btc_pubkey pubkey;
    btc_pubkey_init(&pubkey);
    btc_pubkey_from_key(&requestKey, &pubkey);

    if (btc_pubkey_verify_sig(&pubkey, hash, sig, outlen) != 1)
        return std::string();

    btc_pubkey_cleanse(&pubkey);

    hashOut = DBB::HexStr(hash, hash + 32);
    return DBB::HexStr(sig, sig + outlen);
};

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

static
int logprint_cb(CURL *handle, curl_infotype type,
             char *data, size_t size,
             void *userp)
{
    const char *text;
    (void)handle; /* prevent compiler warning */

    switch (type) {
        case CURLINFO_TEXT:
            DBB::LogPrintDebug(std::string(data));
            printf(data);
        default: /* in case a new one is introduced to shock us */
            return 0;

        case CURLINFO_HEADER_OUT:
            text = "=> Send header";
            break;
        case CURLINFO_DATA_OUT:
            text = "=> Send data";
            break;
        case CURLINFO_SSL_DATA_OUT:
            text = "=> Send SSL data";
            break;
        case CURLINFO_HEADER_IN:
            text = "<= Recv header";
            break;
        case CURLINFO_DATA_IN:
            text = "<= Recv data";
            break;
        case CURLINFO_SSL_DATA_IN:
            text = "<= Recv SSL data";
            break;
    }

    DBB::LogPrintDebug(std::string(data));
    printf(data);
    return 0;
}

bool BitPayWalletClient::SendRequest(const std::string& method,
                                     const std::string& url,
                                     const std::string& args,
                                     std::string& responseOut,
                                     long& httpcodeOut,
                                     bool bws)
{
    CURL* curl;
    CURLcode res;

    bool success = false;

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if (curl) {
        struct curl_slist* chunk = NULL;
        std::string hashOut;
        std::string signature = bws ? SignRequest(method, url, args, hashOut) : "";
        if (signature.empty() && bws) {
            BP_LOG_MSG("SignRequest failed.");
            DBB::LogPrint("SignRequest failed.", "");
            success = false;
        } else {
            if (bws) {
                chunk = curl_slist_append(chunk, ("x-identity: " + GetCopayerId()).c_str()); //requestPubKey).c_str());
                chunk = curl_slist_append(chunk, ("x-signature: " + signature).c_str());
                chunk = curl_slist_append(chunk, ("x-client-version: dbb-1.0.0"));
            }
            chunk = curl_slist_append(chunk, "Content-Type: application/json");
            res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
            curl_easy_setopt(curl, CURLOPT_URL, (bws ? (baseURL + url) : url).c_str());
            if (method == "post")
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, args.c_str());

            if (method == "delete") {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, args.c_str());
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
            }

            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseOut);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

            if (socks5ProxyURL.size())
                curl_easy_setopt(curl, CURLOPT_PROXY, socks5ProxyURL.c_str());

            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 45L);

#if defined(__linux__) || defined(__unix__)
            //need to libcurl, load it once, set the CA path at runtime
            //we assume only linux needs CA fixing
            curl_easy_setopt(curl, CURLOPT_CAINFO, ca_file.c_str());
#endif

#ifdef DBB_ENABLE_NETDEBUG
            curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
            curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, logprint_cb);
#endif

            res = curl_easy_perform(curl);
            if (res != CURLE_OK) {
                BP_LOG_MSG("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
                DBB::LogPrint("curl_easy_perform() failed "+ ( curl_easy_strerror(res) ? std::string(curl_easy_strerror(res)) : "") + "\n", "");
                success = false;
            } else {
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpcodeOut);
                success = true;
            }
        }
        curl_slist_free_all(chunk);
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();

    BP_LOG_MSG("response: %s", responseOut.c_str());
    DBB::LogPrintDebug("response: "+responseOut, "");
    return success;
};

bool BitPayWalletClient::IsSeeded()
{
    std::unique_lock<std::recursive_mutex> lock(this->cs_client);

    if (masterPubKey.size() > 100 && btc_privkey_is_valid(&requestKey))
        return true;
    //TODO check request key
    //TODO check base58 check of masterPubKey (tpub/xpub)

    return false;
}

const std::string BitPayWalletClient::localDataFilename(const std::string& dataDir)
{
   return dataDir + "/" + (testnet ? "testnet_" : "" ) + filenameBase + ".dat";
}

void BitPayWalletClient::SaveLocalData()
{
    std::unique_lock<std::recursive_mutex> lock(this->cs_client);

    //TODO, write a proper generic serialization class (or add a keystore/database to libbtc)
    FILE* writeFile = fopen(localDataFilename(dataDir).c_str(), "wb");
    if (writeFile) {
        unsigned char header[2] = {0xAA, 0xF0};
        fwrite(header, 1, 2, writeFile);
        fwrite(requestKey.privkey, 1, 32, writeFile);
        uint32_t masterPubKeylen = masterPubKey.size();
        fwrite(&masterPubKeylen, 1, sizeof(masterPubKeylen), writeFile);
        fwrite(&masterPubKey.front(), 1, masterPubKeylen, writeFile);

        uint32_t lastKnownAddressLength = lastKnownAddressJson.size();
        fwrite(&lastKnownAddressLength, 1, sizeof(lastKnownAddressLength), writeFile);
        fwrite(&lastKnownAddressJson.front(), 1, lastKnownAddressLength, writeFile);
        
        fwrite(&walletJoined, 1, sizeof(walletJoined), writeFile);
    }
    fclose(writeFile);
}

void BitPayWalletClient::LoadLocalData()
{
    std::unique_lock<std::recursive_mutex> lock(this->cs_client);
    FILE* fh = fopen(localDataFilename(dataDir).c_str(), "rb");

    //TODO: better error handling, misses fclose!
    if (fh) {
        unsigned char header[2];
        if (fread(&header, 1, 2, fh) != 2)
            return;
        if (header[0] != 0xAA || header[1] != 0xF0)
            return;

        if (fread(&requestKey.privkey, 1, 32, fh) != 32)
            return;

        uint32_t masterPubKeylen = 0;
        if (fread(&masterPubKeylen, 1, sizeof(masterPubKeylen), fh) != sizeof(masterPubKeylen))
            return;

        assert(masterPubKeylen < 1024);
        masterPubKey.resize(masterPubKeylen);

        if (fread(&masterPubKey[0], 1, masterPubKeylen, fh) != masterPubKeylen)
            return;

        uint32_t lastKnownAddressLength = 0;
        if (fread(&lastKnownAddressLength, 1, sizeof(lastKnownAddressLength), fh) != sizeof(lastKnownAddressLength))
            return;

        //TODO: better file corruption handling
        if (lastKnownAddressLength < 4096) {
            lastKnownAddressJson.resize(lastKnownAddressLength);
            if (fread(&lastKnownAddressJson[0], 1, lastKnownAddressLength, fh) != lastKnownAddressLength)
                return;
        } else
            lastKnownAddressJson = "";
        
        if (fread(&walletJoined, 1, sizeof(walletJoined), fh) != sizeof(walletJoined))
            return;

        fclose(fh);
    }
}

void BitPayWalletClient::RemoveLocalData()
{
    std::unique_lock<std::recursive_mutex> lock(this->cs_client);
    remove(localDataFilename(dataDir).c_str());
    setNull();
}

void BitPayWalletClient::setNull()
{
    std::unique_lock<std::recursive_mutex> lock(this->cs_client);

    filenameBase.clear();
    masterPubKey.clear();
    masterPubKey.clear();
    memset(requestKey.privkey,0, 32);
    walletJoined = false;
}

int BitPayWalletClient::CheapRandom()
{
    srand((unsigned)time(0));
    return rand()%1000000;
}

void BitPayWalletClient::setCAFile(const std::string& ca_file_in)
{
    ca_file = ca_file_in;
}

void BitPayWalletClient::setSocks5ProxyURL(const std::string& proxyIn)
{
    socks5ProxyURL = proxyIn;
}
