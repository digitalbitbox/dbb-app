// Copyright (c) 2015 Jonas Schnelli
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DBBDAEMON_QT_DAEMONGUI_H
#define DBBDAEMON_QT_DAEMONGUI_H

#include <QMainWindow>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QStandardItemModel>
#include <QUuid>

#include <functional>
#include <thread>
#include <mutex>

#ifdef WIN32
#include <windows.h>
#include "mingw/mingw.mutex.h"
#include "mingw/mingw.condition_variable.h"
#include "mingw/mingw.thread.h"
#endif

#include "config/_dbb-config.h"

#include "dbb_app.h"
#include "dbb_configdata.h"
#include "update.h"
#include "dbb_wallet.h"

#include "dbb_comserver.h"

#ifdef DBB_USE_MULTIMEDIA
#include "qrcodescanner.h"
#endif

#include "backupdialog.h"
#include "getaddressdialog.h"
#include "paymentproposal.h"
#include "signconfirmationdialog.h"
#include "verificationdialog.h"
#include "settingsdialog.h"

#define WALLET_POLL_TIME 25000

namespace Ui
{
class MainWindow;
}

static bool DBB_USE_TESTNET = false;
//DBB USB response types
typedef enum DBB_RESPONSE_TYPE {
    DBB_RESPONSE_TYPE_UNKNOWN,
    DBB_RESPONSE_TYPE_PASSWORD,
    DBB_RESPONSE_TYPE_XPUB_MS_MASTER,
    DBB_RESPONSE_TYPE_XPUB_MS_REQUEST,
    DBB_RESPONSE_TYPE_XPUB_SW_MASTER,
    DBB_RESPONSE_TYPE_XPUB_SW_REQUEST,
    DBB_RESPONSE_TYPE_CREATE_WALLET,
    DBB_RESPONSE_TYPE_INFO,
    DBB_RESPONSE_TYPE_ERASE,
    DBB_RESPONSE_TYPE_LED_BLINK,
    DBB_RESPONSE_TYPE_ADD_BACKUP,
    DBB_RESPONSE_TYPE_LIST_BACKUP,
    DBB_RESPONSE_TYPE_ERASE_BACKUP,
    DBB_RESPONSE_TYPE_VERIFY_BACKUP,
    DBB_RESPONSE_TYPE_RANDOM_NUM,
    DBB_RESPONSE_TYPE_DEVICE_LOCK,
    DBB_RESPONSE_TYPE_VERIFYPASS_ECDH,
    DBB_RESPONSE_TYPE_XPUB_VERIFY,
    DBB_RESPONSE_TYPE_XPUB_GET_ADDRESS,
    DBB_RESPONSE_TYPE_BOOTLOADER_UNLOCK,
    DBB_RESPONSE_TYPE_BOOTLOADER_LOCK,
    DBB_RESPONSE_TYPE_SET_DEVICE_NAME,
    DBB_RESPONSE_TYPE_SET_DEVICE_NAME_CREATE,
    DBB_RESPONSE_TYPE_SET_DEVICE_NAME_RECREATE,
    DBB_RESPONSE_TYPE_RESET_PASSWORD,
    DBB_RESPONSE_TYPE_RESET_U2F,
} dbb_response_type_t;

typedef enum DBB_ADDRESS_STYLE {
    DBB_ADDRESS_STYLE_MULTISIG_1OF1,
    DBB_ADDRESS_STYLE_P2PKH
} dbb_address_style_t;

typedef enum DBB_PROCESS_INFOLAYER_STYLE {
    DBB_PROCESS_INFOLAYER_STYLE_NO_INFO,
    DBB_PROCESS_INFOLAYER_STYLE_TOUCHBUTTON,
    DBB_PROCESS_INFOLAYER_STYLE_REPLUG,
    DBB_PROCESS_INFOLAYER_CONFIRM_WITH_BUTTON,
    DBB_PROCESS_INFOLAYER_CONFIRM_WITH_BUTTON_WARNING,
    DBB_PROCESS_INFOLAYER_UPGRADE_FIRMWARE
} dbb_process_infolayer_style_t;

typedef enum DBB_LED_BLINK_MODE {
    DBB_LED_BLINK_MODE_BLINK,
    DBB_LED_BLINK_MODE_ABORT
} dbb_led_blink_mode_t;

class DBBDaemonGui : public QMainWindow
{
    Q_OBJECT

public:
    explicit DBBDaemonGui(const QString& uri, QWidget* parent = 0);
    ~DBBDaemonGui();

public slots:

signals:
    //emitted when the device state has chaned (connected / disconnected)
    void deviceStateHasChanged(bool state, int deviceType);
    //emitted when the network activity has changed
    void changeNetLoading(bool state);
    //emitted when the DBB could generate a xpub
    void XPubForCopayWalletIsAvailable(int walletIndex);
    //emitted when the request xpub key is available
    void RequestXPubKeyForCopayWalletIsAvailable(int walletIndex);
    //emitted when a response from the DBB is available
    void gotResponse(const UniValue& response, dbb_cmd_execution_status_t status, dbb_response_type_t tag, int subtag = 0);
    //emitted when a copay getwallet response is available
    void getWalletsResponseAvailable(DBBWallet* wallet, bool walletsAvailable, const std::string& walletsResponse, bool initialJoin = false);
    //emitted when a copay wallet history response is available
    void getTransactionHistoryAvailable(DBBWallet* wallet, bool historyAvailable, const UniValue& historyResponse);
    //emitted when a payment proposal and a given signatures should be verified
    void shouldVerifySigning(DBBWallet*, const UniValue& paymentProposal, int actionType, const std::string& signature);
    //emitted when the verification dialog shoud hide
    void shouldHideVerificationInfo();
    //emitted when signatures for a payment proposal are available
    void signedProposalAvailable(DBBWallet*, const UniValue& proposal, const std::vector<std::string>& vSigs);
    //emitted when a wallet needs update
    void shouldUpdateWallet(DBBWallet*);
    //emitted when a new receiving address is available
    void walletAddressIsAvailable(DBBWallet *, const std::string &newAddress, const std::string &keypath);
    //emitted when a new receiving address is available
    void paymentProposalUpdated(DBBWallet *, const UniValue &proposal);
    //emitted when the firmeware upgrade thread is done
    void firmwareThreadDone(bool);
    //emitted when the firmeware upgrade thread is done
    void shouldUpdateModalInfo(const QString& info);
    //emitted when the modal view should be hidden from another thread
    void shouldHideModalInfo();
    void shouldShowAlert(const QString& title, const QString& text);
    //emitted when a tx proposal was successfully created
    void createTxProposalDone(DBBWallet *, const QString &tfaCode, const UniValue &proposal);
    //emitted when a wallet join process was done
    void joinCopayWalletDone(DBBWallet *);

    void comServerIncommingMessage(const QString& msg);

    void reloadGetinfo();

private:
    QString *openedWithBitcoinURI;
    Ui::MainWindow* ui;
    DBBUpdateManager *updateManager;
    BackupDialog* backupDialog;
    GetAddressDialog* getAddressDialog;
    VerificationDialog* verificationDialog;
    SettingsDialog* settingsDialog;
    QTimer *walletUpdateTimer;
    QStandardItemModel *transactionTableModel;
    QLabel* statusBarLabelLeft;
    QLabel* statusBarLabelRight;
    QPushButton* statusBarButton;
    QPushButton* statusBarVDeviceIcon;
    QPushButton* statusBarNetIcon;
    QPushButton* statusBarUSBIcon;
    bool upgradeFirmwareState; //set to true if we expect a firmware upgrade
    bool shouldKeepBootloaderState; //set to true if we expect a firmware upgrade
    QString firmwareFileToUse;
    bool sdcardWarned;
    bool processCommand;
    bool deviceConnected;
    bool cachedWalletAvailableState;
    bool initialWalletSeeding; //state if initial wallet is in seed
    bool cachedDeviceLock;
    bool deviceReadyToInteract;
    bool touchButtonInfo;
    QPropertyAnimation* loginScreenIndicatorOpacityAnimation;
    QPropertyAnimation* netActivityAnimation;
    QPropertyAnimation* usbActivityAnimation;
    QPropertyAnimation* verificationActivityAnimation;
    QString deviceName;
    QString tempNewDeviceName;
    std::string sessionPassword;                    //TODO: needs secure space / mem locking
    std::string sessionPasswordDuringChangeProcess; //TODO: needs secure space / mem locking
    std::vector<DBBWallet*> vMultisigWallets;       //!<immutable pointers to the multisig wallet objects (currently only 1)
    DBBWallet* singleWallet;                        //!<immutable pointer to the single wallet object
    PaymentProposal* currentPaymentProposalWidget;  //!< UI element for showing a payment proposal
    SignConfirmationDialog* signConfirmationDialog;  //!< UI element for showing a payment proposal

    DBB::DBBConfigdata *configData; //!< configuration data model
    DBBComServer *comServer;
    std::time_t lastPing;

    std::atomic<bool> netLoaded;
    std::atomic<int> netErrCount;
#ifdef DBB_USE_MULTIMEDIA
    DBBQRCodeScanner *qrCodeScanner; //!< QRCode scanner object
#endif

    void createMenuBar();

    //== Plug / Unplug ==
    //! gets called when the device was sucessfully unlocked (password accepted)
    void deviceIsReadyToInteract();
    //!enabled or disabled the tabbar
    void setTabbarEnabled(bool status);
    //!enable or disable dbb/usb/loading indication in the UI
    void setLoading(bool status);
    //!resets device infos (in case of a disconnect)
    void resetInfos();
    //! updates the ui after the current device state
    void uiUpdateDeviceState(int deviceType=-1);

    //== UI ==
    void setActiveArrow(int pos);
    //!gets called when the password was accepted by the DBB hardware
    void passwordAccepted();
    //!hides the enter password form
    void hideSessionPasswordView();
    //!update overview flags (wallet / lock, etc)
    void updateOverviewFlags(bool walletAvailable, bool lockAvailable, bool loading);

    //== USB ==
    //wrapper for the DBB USB command action
    bool executeCommandWrapper(const std::string& cmd, const dbb_process_infolayer_style_t layerstyle, std::function<void(const std::string&, dbb_cmd_execution_status_t status)> cmdFinished, const QString& modaltext = "");

    // get a new backup filename
    std::string getBackupString();

    //== Copay Wallet ==
    void hidePaymentProposalsWidget();
    void executeNetUpdateWallet(DBBWallet* wallet, bool showLoading, std::function<void(bool, std::string&)> cmdFinished);


    bool multisigWalletIsUpdating;
    bool singleWalletIsUpdating;
    std::vector<std::pair<std::time_t, std::thread*> > netThreads;
    std::recursive_mutex cs_walletObjects;
    std::thread *fwUpgradeThread;

private slots:
    //!main callback when the device gets connected/disconnected
    void changeConnectedState(bool state, int deviceType);
    //!enable or disable net/loading indication in the UI
    void setNetLoading(bool status);
    //!slot for a periodical update timer
    void updateTimerFired();
    void pingComServer();

    //== UI ==
    //general proxy function to show an alert;
    void showAlert(const QString& title, const QString& errorOut, bool critical = false);
    void mainOverviewButtonClicked();
    void mainMultisigButtonClicked();
    void mainReceiveButtonClicked();
    void mainSendButtonClicked();
    void mainSettingsButtonClicked();
    void gotoOverviewPage();
    void gotoReceivePage();
    void gotoSendCoinsPage();
    void gotoMultisigPage();
    void gotoSettingsPage();
    //!shows info about the smartphone verification
    void showEchoVerification(DBBWallet*, const UniValue& response, int actionType, const std::string& echoStr);
    //!proceed with the signing (2nd step)
    void proceedVerification(const QString& twoFACode, void *ptr, const UniValue& data, int actionType);
    //!hides verification info
    void hideVerificationInfo();
    //!gets called when the user hits enter in the "enter password form"
    void passwordProvided();
    //!slot to ask for the current session password
    void askForSessionPassword();
    //!show general modal info
    void showModalInfo(const QString &info, int helpType = 0);
    void updateModalInfo(const QString &info);
    //!show QRCode in modal view
    void updateModalWithQRCode(const QString& string);
    //!show a new Icon in the modal info
    void updateModalWithIconName(const QString& filename);
    void hideModalInfo();
    void modalStateChanged(bool state);
    //!gets called when the user presses button in the "set password form"
    void setPasswordProvided(const QString& newPassword, const QString& repeatPassword);
    //!gets called on device reset
    void setDeviceNamePasswordProvided(const QString& newPassword, const QString& newName);
    void cleanseLoginAndSetPassword();
    //!gets called when checking for an update
    void updateButtonSetAvailable(bool);

    //== DBB USB ==
    //!function is user whishes to erase the DBB
    void eraseClicked();
    //!reset the U2F seed
    void resetU2F();
    void ledClicked(dbb_led_blink_mode_t mode = DBB_LED_BLINK_MODE_BLINK);
    //!get basic informations about the connected DBB
    void getInfo();
    //!seed the wallet, flush everything and create a new bip32 entropy
    void seedHardware();

    //== DBB USB / UTILS ==
    //!get the current local IPv4 address
    QString getIpAddress();
    //!get a random number from the dbb
    void getRandomNumber();
    //!lock the device, disabled "backup", "verifypass" and "seed" command
    void lockDevice();
    //!lock the bootloader to protect from unecpected firmware upgrades
    void lockBootloader();
    //!open a file chooser and unlock the bootloader
    void upgradeFirmwareButton();
    void upgradeFirmware(bool unlockBootload);
    void noDeviceConnectedLabelLink(const QString& link);
    //!start upgrading the firmware with a file at given location
    void upgradeFirmwareWithFile(const QString& fileName);
    void upgradeFirmwareDone(bool state);
    //!change device name via modal display
    void setDeviceNameProvided(const QString &name);
    //!change device name via popup window
    void setDeviceNameClicked();
    void setDeviceName(const QString& newDeviceName, dbb_response_type_t response_type);
    void parseBitcoinURI(const QString& bitcoinurl, QString& addressOut, QString& amountOut);

    //== ADDRESS EXPORTING ==
    void showGetAddressDialog();
    void getAddressGetXPub(const QString& keypath);
    void getAddressVerify(const QString& keypath);

    //== DBB USB / BACKUP ==
    void showBackupDialog();
    void addBackup();
    void listBackup();
    void eraseAllBackups();
    void eraseBackup(const QString& backupFilename);
    void verifyBackup(const QString& backupFilename);
    void restoreBackup(const QString& backupFilename);

    //== DBB USB Commands (Response Parsing) ==
    //!main function to parse a response from the DBB
    void parseResponse(const UniValue& response, dbb_cmd_execution_status_t status, dbb_response_type_t tag, int subtag);

    //== Copay Wallet ==
    //!create a single wallet
    void createSingleWallet();
    //!get a new address
    void getNewAddress();
    //!verify a address over the smartphone
    void verifyAddress();
    //!get a xpubkey (generic function)
    void getXPub(const std::string& keypath, dbb_response_type_t response_type = DBB_RESPONSE_TYPE_XPUB_VERIFY, dbb_address_style_t address_type = DBB_ADDRESS_STYLE_P2PKH);
    //!gets called when a new address is available
    void updateReceivingAddress(DBBWallet *wallet, const std::string &newAddress, const std::string &keypath);
    //!display fee calculated based on a payment proposal based on the UI values.
    void displayFee();
    //!check the UI values and create a payment proposal from them, sign and post them
    void createTxProposalPressed();
    //!Report about a submitted payment proposal
    void reportPaymentProposalPost(DBBWallet* wallet, const UniValue& proposal);
    void joinCopayWalletClicked();
    //!initiates a copay multisig wallet join
    void joinMultisigWalletInitiate(DBBWallet*);
    //!gets a xpub key for the copay wallet
    void getXPubKeyForCopay(int walletIndex);
    //!gets a xpub key at the keypath that is used for the request private key
    void getRequestXPubKeyForCopay(int walletIndex);
    //!joins a copay wallet with given xpub key
    void joinCopayWallet(int walletIndex);
    //!joins a copay wallet with given xpub key
    void joinCopayWalletComplete(DBBWallet* wallet);
    //!update a given wallet object
    void updateWallet(DBBWallet* wallet);
    //!update multisig wallets
    void MultisigUpdateWallets(bool initialJoin=false);
    //!update single wallet
    void SingleWalletUpdateWallets(bool showLoading=true);
    //!update the mutisig ui from a getWallets response
    void updateUIMultisigWallets(const UniValue& walletResponse);
    //!update the mutisig ui state (joined or not)
    void updateUIStateMultisigWallets(bool joined);
    //!update the singlewallet ui from a getWallets response
    void updateUISingleWallet(const UniValue& walletResponse);
    //!update the single wallet transaction table
    void updateTransactionTable(DBBWallet *wallet, bool historyAvailable, const UniValue& history);
    //!show tx in a block explorer
    void historyShowTx(QModelIndex index);
    //!parse single wallet wallet response
    void parseWalletsResponse(DBBWallet* wallet, bool walletsAvailable, const std::string& walletsResponse, bool initialJoin=false);
    //!update payment proposals
    bool MultisigUpdatePaymentProposals(const UniValue& response);
    //!show a single payment proposals with given id
    bool MultisigShowPaymentProposal(const UniValue& pendingTxps, const std::string& targetID);
    //!execute payment proposal action
    void PaymentProposalAction(DBBWallet* wallet, const QString &tfaCode, const UniValue& paymentProposal, int actionType = ProposalActionTypeAccept);
    //!post
    void postSignaturesForPaymentProposal(DBBWallet* wallet, const UniValue& proposal, const std::vector<std::string>& vSigs);

    //== Smart Verification Pairing ==
    //!send a ecdh pairing request with pubkey to the DBB
    void sendECDHPairingRequest(const std::string &ecdhRequest);
    void pairSmartphone();
    void comServerMessageParse(const QString& msg);

    //== settings ==
    //!show settings dialog
    void showSettings();
    void updateSettings();
    void updateHiddenPassword(const QString& hiddenPassword);

    //== QRCode Scanner==
    void showQrCodeScanner();
    void qrCodeFound(const QString& payload);

    //== linux udev rule check
    void checkUDevRule();
};

#endif
