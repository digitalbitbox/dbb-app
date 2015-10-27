// Copyright (c) 2015 Jonas Schnelli
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bpwalletclient.h"

#include "config/_dbb-config.h"

#include <algorithm>

#include <assert.h>
#include <string.h>

#include "libdbb/crypto.h"
#include "dbb_util.h"

#include <boost/filesystem.hpp>

#include <btc/base58.h>
#include <btc/ecc_key.h>
#include <btc/ecc.h>
#include <btc/hash.h>
#include <btc/bip32.h>
#include <btc/tx.h>


//ignore osx depracation warning
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

#include <climits>

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

BitPayWalletClient::BitPayWalletClient()
{
//    requestKey = NULL;

    baseURL = "https://bws.bitpay.com/bws/api";
}

BitPayWalletClient::~BitPayWalletClient()
{
}

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
    std::string requestKeyHex;
    if (!GetRequestPubKey(requestKeyHex))
        return false;

    out = _copayerHash(name, masterPubKey, requestKeyHex);
    return true;
};

bool BitPayWalletClient::Hash(const std::string &stringIn, uint8_t *hashout)
{
    const char *s = stringIn.c_str();
    btc_hash((const uint8_t *)s, stringIn.size(), hashout);
}

bool BitPayWalletClient::GetCopayerSignature(const std::string& stringToHash, const uint8_t *privKey, std::string& sigHexOut)
{
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

    assert(btc_pubkey_verify_sig(&pubkey, hash2, sig, outlen) == 1);

    btc_privkey_cleanse(&key);
    btc_pubkey_cleanse(&pubkey);

    std::vector<unsigned char> signature;
    signature.assign(sig, sig + outlen);

    sigHexOut = DBB::HexStr(sig, sig + outlen);
    return true;
};

void BitPayWalletClient::setMasterPubKey(const std::string& xPubKey)
{
    masterPubKey = xPubKey;

    SaveLocalData();
}

void BitPayWalletClient::setRequestPubKey(const std::string& xPubKeyRequestKeyEntropy)
{
//    //now this is a ugly workaround because we need a request keypair (pub/priv)
//    //for signing the requests after BitAuth
//    //Signing over the hardware wallet would be a very bad UX (press button on
//    // every request) and it would be slow
//    //the request key should be deterministic and linked to the master key
//    //
//    //we now generate a private key by (miss)using the xpub at m/1'/0' as entropy
//    //for a new private key

    btc_hdnode node;
    bool r = btc_hdnode_deserialize(xPubKeyRequestKeyEntropy.c_str(), &btc_chain_test, &node);

    memcpy(requestKey.privkey, node.public_key+1, 32);
    std::vector<unsigned char> hash = DBB::ParseHex("26db47a48a10b9b0b697b793f5c0231aa35fe192c9d063d7b03a55e3c302850a");

    unsigned char sig[74];
    size_t outlen = 74;
    assert(btc_key_sign_hash(&requestKey, &hash.front(), sig, &outlen) == 1);


    btc_pubkey pubkey;
    btc_pubkey_init(&pubkey);
    btc_pubkey_from_key(&requestKey, &pubkey);

    unsigned int i;
    for(i = 33; i < BTC_ECKEY_UNCOMPRESSED_LENGTH; i++)
        assert(pubkey.pubkey[i] == 0);


    assert(btc_pubkey_verify_sig(&pubkey, &hash.front(), sig, outlen) == 1);
    btc_pubkey_cleanse(&pubkey);

    SaveLocalData();
}

void BitPayWalletClient::seed()
{
//    CKeyingMaterial vSeed;
//    vSeed.resize(32);
//
//    RandAddSeedPerfmon();
//    do {
//        GetRandBytes(&vSeed[0], vSeed.size());
//    } while (!eccrypto::Check(&vSeed[0]));
//
//    CExtKey masterPrivKeyRoot;
//    masterPrivKeyRoot.SetMaster(&vSeed[0], vSeed.size()); //m
//    masterPrivKeyRoot.Derive(masterPrivKey, 45);          //m/45' xpriv
//    masterPubKey = masterPrivKey.Neuter();                //m/45' xpub
//
//    CExtKey requestKeyChain;
//    masterPrivKeyRoot.Derive(requestKeyChain, 1); //m/1'
//
//    CExtKey requestKeyExt;
//    requestKeyChain.Derive(requestKeyExt, 0);
//
//    requestKey = requestKeyExt.key;
//    printf("seed: request key: %s\n", HexStr(requestKey.begin(), requestKey.end(), false).c_str());
}

bool BitPayWalletClient::GetRequestPubKey(std::string& pubKeyOut)
{
    btc_pubkey pubkey;
    btc_pubkey_init(&pubkey);
    btc_pubkey_from_key(&requestKey, &pubkey);

    pubKeyOut = DBB::HexStr(pubkey.pubkey, pubkey.pubkey+33);

    btc_pubkey_cleanse(&pubkey);

    return true;
}

std::string BitPayWalletClient::GetCopayerId()
{
    uint8_t hashout[32];
    btc_hash_sngl_sha256((const uint8_t *)masterPubKey.c_str(), masterPubKey.size(), hashout);
    return DBB::HexStr(hashout, hashout + 32);
}

bool BitPayWalletClient::ParseWalletInvitation(const std::string& walletInvitation, BitpayWalletInvitation& invitationOut)
{
    std::vector<int> splits = {22, 74};
    std::vector<std::string> secretSplit = split(walletInvitation, splits);

    //TODO: var widBase58 = secretSplit[0].replace(/0/g, '');
    std::string widBase58 = secretSplit[0];
    std::vector<unsigned char> vch;

    size_t buflen = widBase58.size();
    uint8_t buf[buflen*3];
    if (!btc_base58_decode(buf, &buflen, widBase58.c_str()))
        return false;

    std::string widHex = DBB::HexStr(buf+6,buf+6+buflen, false);

    splits = {8, 12, 16, 20};
    std::vector<std::string> walletIdParts = split(widHex, splits);
    invitationOut.walletID = walletIdParts[0] + "-" + walletIdParts[1] + "-" + walletIdParts[2] + "-" + walletIdParts[3] + "-" + walletIdParts[4];

    uint8_t rawn[34];
    std::string walletPrivKeyStr = secretSplit[1];
    printf("wallet pkey: %s\n", walletPrivKeyStr.c_str());
    int r = btc_base58_decode_check(walletPrivKeyStr.c_str(), rawn, 34);


    memcpy(invitationOut.walletPrivKey, &rawn[1], 32);
    printf("wallet pkey hex: %s\n", DBB::HexStr(invitationOut.walletPrivKey, invitationOut.walletPrivKey+32).c_str());
    invitationOut.network = secretSplit[2] == "T" ? "testnet" : "livenet";
    return true;
}

bool BitPayWalletClient::JoinWallet(const std::string& name, const std::string& walletInvitation, std::string& response)
{
    std::string requestPubKey;
    if (!GetRequestPubKey(requestPubKey))
        return false;

    BitpayWalletInvitation invitation;
    if (!ParseWalletInvitation(walletInvitation, invitation))
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

    std::string searchS = "\"isTemporaryRequestKey\":0";
    std::string newStr = "\"isTemporaryRequestKey\":false";
    size_t pos = 0;
    while((pos = json.find(searchS, pos)) != std::string::npos){
        json.replace(pos, searchS.length(), newStr);
        pos += newStr.length();
    }



    long httpStatusCode = 0;
    SendRequest("post", "/v1/wallets/" + invitation.walletID + "/copayers", json, response, httpStatusCode);


    std::string getWalletsResponse;
    GetWallets(getWalletsResponse);

    if (httpStatusCode != 200)
        return false;

    return true;
}

bool BitPayWalletClient::GetWallets(std::string& response)
{
    std::string requestPubKey;
    if (!GetRequestPubKey(requestPubKey))
        return false;

    long httpStatusCode = 0;
    SendRequest("get", "/v1/wallets/?r=16354", "{}", response, httpStatusCode);

    if (httpStatusCode != 200)
        return false;

    return true;
}

std::string BitPayWalletClient::ParseTxProposal(const UniValue& txProposal, std::vector<std::pair<std::string, std::vector<unsigned char>> >& vInputTxHashes)
{
    btc_tx *tx = btc_tx_new();
    
    std::vector<std::string> keys = txProposal.getKeys();
    std::vector<UniValue> values = txProposal.getValues();

    std::string toAddress;
    int64_t toAmount = -1;
    int64_t fee = -1;
    std::vector<int> outputOrder;
    int requiredSignatures = -1;
    int i = 0;
    int j = 0;
    for (i = 0; i < keys.size(); i++) {
        UniValue val = values[i];

        if (keys[i] == "toAddress")
            toAddress = val.get_str();

        if (keys[i] == "amount")
            toAmount = val.get_int64();

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
    int64_t inTotal = 0;

    std::vector<std::pair<std::string, std::vector<unsigned char> > > inputsScriptAndPath;
    for (i = 0; i < inputs.size(); i++) {
        UniValue aInput = inputs[i];
        std::vector<std::string> keys = aInput.getKeys();
        std::vector<UniValue> values = aInput.getValues();

        std::string txId;
        std::vector<std::string> publicKeys;
        std::string path;
        std::vector<unsigned char> script;
        int nInput = -1;

        for (j = 0; j < keys.size(); j++) {
            UniValue val = values[j];
            if (keys[j] == "txid")
                txId = val.get_str();

            if (keys[j] == "vout")
                nInput = val.get_int();

            if (keys[j] == "satoshis")
                inTotal = val.get_int64();

            if (keys[j] == "path")
                path = val.get_str();

            if (keys[j] == "publicKeys") {
                std::vector<UniValue> pubKeyValue = val.getValues();
                int k;
                for (k = 0; k < pubKeyValue.size(); k++) {
                    UniValue aPubKeyObj = pubKeyValue[k];
                    publicKeys.push_back(aPubKeyObj.get_str());
                    printf("pubkey: %s\n", aPubKeyObj.get_str().c_str());
                }

                //sort keys
                std::sort(keys.begin(), keys.end());
            }
        }

        script.insert(script.end(), 0x00);
        script.insert(script.end(), 0x4c);
        script.insert(script.end(), 0x69);

        vector *v_pubkeys = vector_new(3, NULL);
        for (i = publicKeys.size()-1; i >= 0 ; i--) {
            btc_pubkey *pubkey = (btc_pubkey *)malloc(sizeof(btc_pubkey));
            btc_pubkey_init(pubkey);
            std::vector<unsigned char> data = DBB::ParseHex(publicKeys[i]);

            //TODO: allow uncompressed keys
            pubkey->compressed = true;
            memcpy(pubkey->pubkey, &data[0], 33);
            vector_add(v_pubkeys, pubkey);
        }

        cstring *msscript = cstr_new_sz(1024);
        btc_script_build_multisig(msscript, requiredSignatures, v_pubkeys);
        vector_free(v_pubkeys, true);

        script.insert(script.end(), msscript->str, msscript->str + msscript->len);

        std::vector<unsigned char> msP2SHScript(msscript->len);
        msP2SHScript.assign(msscript->str, msscript->str + msscript->len);
        path.erase(0, 2); //remove m/ from path
        inputsScriptAndPath.push_back(std::make_pair(path, msP2SHScript));

        std::vector<unsigned char> aHash = DBB::ParseHex(ReversePairs(txId));

        btc_tx_in *txin = btc_tx_in_new();
        memcpy(txin->prevout.hash, &aHash[0], 32);
        txin->prevout.n = nInput;
        txin->script_sig = cstr_new_sz(script.size());
        cstr_append_buf(txin->script_sig, &script[0], script.size());
        vector_add(tx->vin, txin);


        // find out change address
        UniValue changeAddrObj = find_value(txProposal, "changeAddress");
        keys = changeAddrObj.getKeys();
        values = changeAddrObj.getValues();
        std::string changeAdr = "";
        for (i = 0; i < keys.size(); i++) {
            UniValue val = values[i];

            if (keys[i] == "address")
                changeAdr = val.get_str();
        }


        btc_tx_add_address_out(tx, &btc_chain_test, inTotal - toAmount - fee, changeAdr.c_str());
        btc_tx_add_address_out(tx, &btc_chain_test, toAmount, toAddress.c_str());


        cstring *txser = cstr_new_sz(1024);
        btc_tx_serialize(txser, tx);
        std::string sSigDER = DBB::HexStr((unsigned char *)txser->str, (unsigned char *)txser->str + txser->len);
        printf("%s\n", sSigDER.c_str());
        int cnt = 0;

        for (cnt = 0; cnt < tx->vin->len; cnt++) {
            std::pair<std::string, std::vector<unsigned char> > scriptAndPath = inputsScriptAndPath[cnt];
            std::vector<unsigned char> aScript = scriptAndPath.second;

            cstring *new_script = cstr_new_buf(&aScript[0], aScript.size());
            uint8_t hash[32];
            btc_tx_sighash(tx, new_script, 0, 1, hash);

            std::string sSigDER2 = DBB::HexStr((unsigned char *)hash, (unsigned char *)hash + 32);
            printf("%s\n", sSigDER2.c_str());

            //uint256 hash = SignatureHash(aScript, t, 0, SIGHASH_ALL);
            std::vector<unsigned char> vHash(32);
            vHash.assign(hash, hash+32);
            vInputTxHashes.push_back(std::make_pair(scriptAndPath.first, vHash));
            cnt++;
        }
    }
}

int ecdsa_sig_to_der(const uint8_t* sig, uint8_t* der)
{
    int i;
    uint8_t* p = der, *len, *len1, *len2;
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
        unsigned char sig[74];
        int sizeN = ecdsa_sig_to_der(&data[0], sig);
        std::string sSigDER = DBB::HexStr(sig, sig + sizeN);
        sigs.push_back(sSigDER);
    }
    signaturesRequest.push_back(Pair("signatures", sigs));
    std::string response;
    long httpStatusCode = 0;
    SendRequest("post", "/v1/txproposals/" + txpID + "/signatures/", signaturesRequest.write(), response, httpStatusCode);
    if (httpStatusCode != 200)
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
    SendRequest("post", "/v1/txproposals/" + txpID + "/broadcast/", "{}", response, httpStatusCode);
    printf("Response: %s\n", response.c_str());
    if (httpStatusCode != 200)
        return false;

    return true;
}


std::string BitPayWalletClient::SignRequest(const std::string& method,
                                            const std::string& url,
                                            const std::string& args, std::string& hashOut)
{
    std::string message = method + "|" + url + "|" + args;
    uint8_t hash[32];
    btc_hash((const unsigned char *)&message.front(), message.size(), hash);

    printf("signing message: %s, hash: %s\n", message.c_str(), DBB::HexStr(hash, hash+32).c_str());

    unsigned char sig[74];
    size_t outlen = 74;
    btc_key_sign_hash(&requestKey, hash, sig, &outlen);

    btc_pubkey pubkey;
    btc_pubkey_init(&pubkey);
    btc_pubkey_from_key(&requestKey, &pubkey);

    assert(btc_pubkey_verify_sig(&pubkey, hash, sig, outlen) == 1);

    btc_pubkey_cleanse(&pubkey);

    hashOut = DBB::HexStr(hash, hash+32);
    return DBB::HexStr(sig, sig+outlen);
};

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

bool BitPayWalletClient::SendRequest(const std::string& method,
                                     const std::string& url,
                                     const std::string& args,
                                     std::string& responseOut,
                                     long& httpcodeOut)
{
    CURL* curl;
    CURLcode res;

    bool error = false;

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if (curl) {
        struct curl_slist* chunk = NULL;
        std::string hashOut;
        std::string signature = SignRequest(method, url, args, hashOut);
        chunk = curl_slist_append(chunk, ("x-identity: " + GetCopayerId()).c_str()); //requestPubKey).c_str());
        chunk = curl_slist_append(chunk, ("x-signature: " + signature).c_str());
        chunk = curl_slist_append(chunk, ("x-client-version: dbb-1.0.0"));
        chunk = curl_slist_append(chunk, "Content-Type: application/json");
        res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
        curl_easy_setopt(curl, CURLOPT_URL, (baseURL + url).c_str());
        if (method == "post")
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, args.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseOut);

#ifdef DBB_ENABLE_DEBUG
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
#endif

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            error = true;
        } else {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpcodeOut);
            if (httpcodeOut != 200)
                error = true;
        }

        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();

#ifdef DBB_ENABLE_DEBUG
    printf("response: %s", responseOut.c_str());
#endif

    return error;
};


bool TryCreateDirectory(const boost::filesystem::path& p)
{
    try
    {
        return boost::filesystem::create_directory(p);
    } catch (const boost::filesystem::filesystem_error&) {
        if (!boost::filesystem::exists(p) || !boost::filesystem::is_directory(p))
            throw;
    }

    // create_directory didn't create the directory, it had to have existed already
    return false;
}

#ifdef WIN32
boost::filesystem::path GetSpecialFolderPath(int nFolder, bool fCreate)
{
    namespace fs = boost::filesystem;

    char pszPath[MAX_PATH] = "";

    if(SHGetSpecialFolderPathA(NULL, pszPath, nFolder, fCreate))
    {
        return fs::path(pszPath);
    }

    LogPrintf("SHGetSpecialFolderPathA() failed, could not obtain requested path.\n");
    return fs::path("");
}
#endif

boost::filesystem::path GetDefaultDBBDataDir()
{
    namespace fs = boost::filesystem;
// Windows < Vista: C:\Documents and Settings\Username\Application Data\Bitcoin
// Windows >= Vista: C:\Users\Username\AppData\Roaming\Bitcoin
// Mac: ~/Library/Application Support/Bitcoin
// Unix: ~/.bitcoin
#ifdef WIN32
    // Windows
    return GetSpecialFolderPath(CSIDL_APPDATA) / "DBB";
#else
    fs::path pathRet;
    char* pszHome = getenv("HOME");
    if (pszHome == NULL || strlen(pszHome) == 0)
        pathRet = fs::path("/");
    else
        pathRet = fs::path(pszHome);
#ifdef MAC_OSX
    // Mac
    pathRet /= "Library/Application Support";
    TryCreateDirectory(pathRet);
    return pathRet / "DBB";
#else
    // Unix
    return pathRet / ".dbb";
#endif
#endif
}

bool BitPayWalletClient::IsSeeded()
{
    if (masterPubKey.size() > 100)
        return true;
    //TODO check request key
    //TODO check base58 check of masterPubKey (tpub/xpub)

    return false;
}

void BitPayWalletClient::SaveLocalData()
{
    //TODO, write a proper generic serialization class (or add a keystore/database to libbtc)
    boost::filesystem::path dataDir = GetDefaultDBBDataDir();
    boost::filesystem::create_directories(dataDir);
    FILE* writeFile = fopen((dataDir / "copay.dat").string().c_str(), "wb");
    if (writeFile) {

        unsigned char header[2] = {0xAA, 0xF0};
        fwrite(header, 1, 2, writeFile);
        fwrite(requestKey.privkey, 1, 32, writeFile);
        uint32_t masterPubKeylen = masterPubKey.size();
        fwrite(&masterPubKeylen, 1, sizeof(masterPubKeylen), writeFile);
        fwrite(&masterPubKey.front(), 1, masterPubKeylen, writeFile);
    }
    fclose(writeFile);
}

void BitPayWalletClient::LoadLocalData()
{
    boost::filesystem::path dataDir = GetDefaultDBBDataDir();
    boost::filesystem::create_directories(dataDir);
    FILE* fh = fopen((dataDir / "copay.dat").string().c_str(), "rb");
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

        fclose(fh);
    }
}

void BitPayWalletClient::RemoveLocalData()
{
    boost::filesystem::path dataDir = GetDefaultDBBDataDir();
    boost::filesystem::remove(dataDir / "copay.dat");
}