/*

 The MIT License (MIT)

 Copyright (c) 2015 Jonas Schnelli

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

#ifndef BP_WALLET_CLIENT_H
#define BP_WALLET_CLIENT_H

#include <stdio.h>
#include <string>

#include <curl/curl.h>

#include <assert.h>
#include <cstring>
#include <mutex>
#ifdef WIN32
#include <windows.h>
#include "mingw/mingw.mutex.h"
#include "mingw/mingw.condition_variable.h"
#include "mingw/mingw.thread.h"
#endif
#include <stdexcept>
#include <stdint.h>
#include <string>
#include <vector>

#include <univalue.h>

#include <btc/ecc_key.h>

//!tiny class for a bitpay wallet service wallet invitation
class BitpayWalletInvitation
{
public:
    std::string walletID;
    uint8_t walletPrivKey[32];
    std::string network;
};


class BitPayWalletClient
{
public:
    BitPayWalletClient(std::string dataDirIn, bool testnetIn = false);
    ~BitPayWalletClient();

    //!set the base URL (for the BWS)
    void setBaseURL(const std::string& newBaseURL);

    //!set the filename-base to store local data
    void setFilenameBase(const std::string& filenameBaseIn);

    //!get the filename-base to store local data
    const std::string& getFilenameBase();

    //!parse a wallet invitation code
    bool ParseWalletInvitation(const std::string& walletInvitation, BitpayWalletInvitation& invitationOut);

    //!exports the extended request key (base58check), returns true in operation was successfull
    bool GetRequestPubKey(std::string& pubKeyOut);

    //!get the copyer id as string
    std::string GetCopayerId();

    //!generates the copayer hash (name, xpub, request key)
    bool GetCopayerHash(const std::string& name, std::string& hashOut);

    //!signs a given string with a given key
    bool GetCopayerSignature(const std::string& stringToHash, const uint8_t* privKey, std::string& sigHexOut);

    //!Create a wallet
    bool CreateWallet(const std::string& walletName);

    //!Get a new address and writes it to &newAddress
    bool GetNewAddress(std::string& newAddress, std::string& keypath);

    //!Return the last (disk) cached known address for receiving coins
    bool GetLastKnownAddress(std::string& address, std::string& keypath);

    //!Creates a payment proposal
    //! If amount is set to 0, it tired to create a proposal with all available funds
    bool CreatePaymentProposal(const std::string& address, uint64_t amount, uint64_t feeperkb, UniValue& paymentProposalOut, std::string& errorOut, bool checkSingleOutput = false);

    bool PublishTxProposal(const UniValue& paymentProposal, std::string& errorOut);

    //!joins a Wopay wallet
    bool JoinWallet(const std::string& name, const BitpayWalletInvitation invitation, std::string& response);

    //!get the current mainnet fee levels
    bool GetFeeLevels();

    //!get the feeperkb rate for the given prio (0 = priority, 1 = normal, 2 = economy)
    int64_t GetFeeForPriority(int prio = 0);

    //!load available wallets over wallet server
    bool GetWallets(std::string& response);

    //!load transaction history
    bool GetTransactionHistory(std::string& response);

    //!parse a transaction proposal, export inputs keypath/hashes ready for signing
    void ParseTxProposal(const UniValue& txProposal, UniValue& changeAddressData, std::string& serTx, std::vector<std::pair<std::string, std::vector<unsigned char> > >& vInputTxHashes, bool noScriptPubKey = false);

    //!post signatures for a transaction proposal to the wallet server
    bool PostSignaturesForTxProposal(const UniValue& txProposal, const std::vector<std::string>& vHexSigs);

    //!post a tx proposal reject
    bool RejectTxProposal(const UniValue& txProposal);

    //!delete a tx proposal reject
    bool DeleteTxProposal(const UniValue& txProposal);

    //!tells the wallet server that we'd like to broadcast a txproposal (make sure tx proposal has enought signatures)
    bool BroadcastProposal(const UniValue& txProposal);

    //!returns the root xpub key (mostly m/45')
    std::string GetXPubKey();

    //!sign a http request (generates x-signature header string)
    std::string SignRequest(const std::string& method,
                            const std::string& url,
                            const std::string& args,
                            std::string& hashOut);

    //!send a request to the wallet server
    bool SendRequest(const std::string& method,
                     const std::string& url,
                     const std::string& args,
                     std::string& responseOut,
                     long& httpStatusCodeOut);

    //!set the master extended public key
    void setMasterPubKey(const std::string& xPubKey);

    //!set the request pubkey over a xpubkey as deterministic entropy
    void setRequestPubKey(const std::string& xPubKeyRequestKeyEntropy);

    //!returns true in case of an available xpub/request key
    bool IsSeeded();
    
    //!whether or not a Copay wallet was joined with the xpub/request key
    bool walletJoined;

    //!local filename (absolute)
    const std::string localDataFilename(const std::string& dataDir);

    //!store local data (xpub key, request key, etc.)
    void SaveLocalData();

    //!retrive local data
    void LoadLocalData();

    //!remove local data (remove file)
    void RemoveLocalData();

    //!reset the object, allow to refill the initial data
    void setNull();

    //flip byte order, required to reverse a given LE hash in hex to BE
    static std::string ReversePairs(const std::string& strIn);

    //set the dynamic/runtime CA file for https requests
    void setCAFile(const std::string& ca_file);

    void setSocks5ProxyURL(const std::string& proxyUrl);

    int CheapRandom();

private:
    std::recursive_mutex cs_client;
    const std::string dataDir;
    const bool testnet;
    std::string masterPrivKey; // "m/45'"
    std::string masterPubKey;  // "m/45'"
    btc_key requestKey;        //"m/1'/0"

    std::string filenameBase;
    std::string baseURL;              //!< base URL for the wallet server
    std::string lastKnownAddressJson; //!< base URL for the wallet server

    std::vector<std::string> split(const std::string& str, std::vector<int> indexes);
    std::string _copayerHash(const std::string& name, const std::string& xPubKey, const std::string& requestPubKey);

    UniValue feeLevelsObject;
    //!Wrapper for libbtcs doubla sha
    void Hash(const std::string& stringIn, uint8_t* hashout);

    std::string ca_file;
    std::string socks5ProxyURL;
};
#endif //BP_WALLET_CLIENT_H
