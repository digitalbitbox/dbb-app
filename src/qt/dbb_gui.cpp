// Copyright (c) 2015 Jonas Schnelli
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "dbb_gui.h"

#include <QAction>
#include <QApplication>
#include <QPushButton>
#include <QDebug>
#include <QDesktopServices>
#include <QFileDialog>
#include <QInputDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QSpacerItem>
#include <QTimer>
#include <QToolBar>
#include <QFontDatabase>
#include <QGraphicsOpacityEffect>
#include <QtNetwork/QHostInfo>
#include <QDateTime>
#include <QSettings>

#include "ui/ui_overview.h"
#include <dbb.h>
#include "libdbb/crypto.h"

#include "dbb_ca.h"
#include "dbb_util.h"
#include "dbb_netthread.h"
#include "serialize.h"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <chrono>
#include <fstream>
#include <iomanip> // put_time

#include <univalue.h>
#include <btc/bip32.h>
#include <btc/tx.h>

#include <qrencode.h>

#include "firmware.h"

#if defined _MSC_VER
#include <direct.h>
#elif defined __GNUC__
#include <sys/types.h>
#include <sys/stat.h>
#endif

const static bool DBB_FW_UPGRADE_DUMMY_SIGN = false;

const static int MAX_INPUTS_PER_SIGN = 14;

//function from dbb_app.cpp
extern void executeCommand(const std::string& cmd, const std::string& password, std::function<void(const std::string&, dbb_cmd_execution_status_t status)> cmdFinished);
extern void setFirmwareUpdateHID(bool state);

// static C based callback which gets called if the com server gets a message
static void comServerCallback(DBBComServer* cs, const std::string& str, void *ctx)
{
    // will be called on the com server thread
    if (ctx)
    {
        DBBDaemonGui *gui = (DBBDaemonGui *)ctx;

        // emits signal "comServerIncommingMessage"
        QMetaObject::invokeMethod(gui, "comServerIncommingMessage", Qt::QueuedConnection,
                                  Q_ARG(QString, QString::fromStdString(str)));
    }
}

DBBDaemonGui::~DBBDaemonGui()
{
#ifdef DBB_USE_MULTIMEDIA
    if (qrCodeScanner) {
        delete qrCodeScanner; qrCodeScanner = NULL;
    }
#endif
    if (backupDialog) {
        delete backupDialog; backupDialog = NULL;
    }
    if (getAddressDialog) {
        delete getAddressDialog; getAddressDialog = NULL;
    }
    if (statusBarButton) {
        delete statusBarButton; statusBarButton = NULL;
    }
    if (statusBarVDeviceIcon) {
        delete statusBarVDeviceIcon; statusBarVDeviceIcon = NULL;
    }
    if (statusBarNetIcon) {
        delete statusBarNetIcon; statusBarNetIcon = NULL;
    }
    if (statusBarUSBIcon) {
        delete statusBarUSBIcon; statusBarUSBIcon = NULL;
    }
    if (statusBarLabelLeft) {
        delete statusBarLabelLeft; statusBarLabelLeft = NULL;
    }
    if (configData) {
        delete configData; configData = NULL;
    }
    {
        std::unique_lock<std::recursive_mutex> lock(this->cs_walletObjects);

        if (singleWallet) {
            delete singleWallet; singleWallet = NULL;
        }
        if (!vMultisigWallets.empty()) {
            delete vMultisigWallets[0];
            vMultisigWallets.clear();
        }
    }
    if (walletUpdateTimer) {
        walletUpdateTimer->stop();
        delete walletUpdateTimer; walletUpdateTimer = NULL;
    }
    if (updateManager) {
        delete updateManager; updateManager = NULL;
    }
    if (comServer) {
        delete comServer; comServer = NULL;
    }
}

DBBDaemonGui::DBBDaemonGui(const QString& uri, QWidget* parent) : QMainWindow(parent),
                                              openedWithBitcoinURI(0),
                                              ui(new Ui::MainWindow),
                                              statusBarButton(0),
                                              statusBarLabelLeft(0),
                                              backupDialog(0),
                                              getAddressDialog(0),
                                              verificationDialog(0),
                                              processCommand(0),
                                              deviceConnected(0),
                                              deviceReadyToInteract(0),
                                              cachedWalletAvailableState(0),
                                              initialWalletSeeding(0),
                                              cachedDeviceLock(0),
                                              currentPaymentProposalWidget(0),
                                              signConfirmationDialog(0),
                                              loginScreenIndicatorOpacityAnimation(0),
                                              netActivityAnimation(0),
                                              usbActivityAnimation(0),
                                              verificationActivityAnimation(0),
                                              sdcardWarned(0),
                                              fwUpgradeThread(0),
                                              upgradeFirmwareState(0),
                                              shouldKeepBootloaderState(0),
                                              touchButtonInfo(0),
                                              walletUpdateTimer(0),
                                              comServer(0),
                                              lastPing(0),
                                              netLoaded(false),
                                              netErrCount(0),
                                              settingsDialog(0),
                                              updateManager(0)
{
#ifdef DBB_USE_MULTIMEDIA
    qrCodeScanner = NULL;
#endif
    initTranslations();

#if defined(Q_OS_MAC)
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    ui->setupUi(this);

#if defined(__linux__) || defined(__unix__)
    std::string ca_file;
    //need to libcurl, load it once, set the CA path at runtime
    ca_file = DBB::getCAFile();
#endif

    //testnet/mainnet switch
    if (DBB::mapArgs.count("-testnet"))
        DBB_USE_TESTNET = true;

    if (!uri.isEmpty())
        openedWithBitcoinURI = new QString(uri);

/////////// UI Styling
#if defined(Q_OS_MAC)
    std::string balanceFontSize = "24pt";
    std::string menuFontSize = "18pt";
#elif defined(Q_OS_WIN)
    std::string balanceFontSize = "20pt";
    std::string menuFontSize = "16pt";
#else
    std::string balanceFontSize = "20pt";
    std::string menuFontSize = "14pt";
#endif

    QFontDatabase::addApplicationFont(":/fonts/BebasKai-Regular");
    this->setStyleSheet("QMainWindow {background: 'white';}");

#if defined(Q_OS_WIN)
    qApp->setStyleSheet("");
#else
    qApp->setStyleSheet("QWidget { font-family: Source Sans Pro; } QHeaderView::section { font-family: Source Sans Pro Black; }");
#endif
    QString buttonCss("QPushButton::hover { } QPushButton:pressed { background-color: rgba(200,200,200,230); border:0; color: white; } QPushButton { font-family: Bebas Kai; font-size:" + QString::fromStdString(menuFontSize) + "; border:0; color: #444444; };");
    QString msButtonCss("QPushButton::hover { } QPushButton:pressed { background-color: rgba(200,200,200,230); border:0; color: #003366; } QPushButton { font-family: Bebas Kai; font-size:" + QString::fromStdString(menuFontSize) + "; border:0; color: #003366; };");



    this->ui->receiveButton->setStyleSheet(buttonCss);
    this->ui->overviewButton->setStyleSheet(buttonCss);
    this->ui->sendButton->setStyleSheet(buttonCss);
    this->ui->mainSettingsButton->setStyleSheet(buttonCss);
    this->ui->multisigButton->setStyleSheet(msButtonCss);

    this->ui->balanceLabel->setStyleSheet("font-size: " + QString::fromStdString(balanceFontSize) + ";");
    this->ui->singleWalletBalance->setStyleSheet("font-size: " + QString::fromStdString(balanceFontSize) + ";");
    this->ui->multisigBalance->setStyleSheet("font-size: " + QString::fromStdString(balanceFontSize) + ";");
    ////////////// END STYLING

    this->ui->tableWidget->setVisible(false);
    this->ui->loadinghistory->setVisible(true);

    // allow serval signaling data types
    qRegisterMetaType<UniValue>("UniValue");
    qRegisterMetaType<std::string>("std::string");
    qRegisterMetaType<dbb_cmd_execution_status_t>("dbb_cmd_execution_status_t");
    qRegisterMetaType<dbb_response_type_t>("dbb_response_type_t");
    qRegisterMetaType<std::vector<std::string> >("std::vector<std::string>");
    qRegisterMetaType<DBBWallet*>("DBBWallet *");

    // connect UI
    connect(ui->noDeviceConnectedLabel, SIGNAL(linkActivated(const QString&)), this, SLOT(noDeviceConnectedLabelLink(const QString&)));
    connect(ui->eraseButton, SIGNAL(clicked()), this, SLOT(eraseClicked()));
    connect(ui->ledButton, SIGNAL(clicked()), this, SLOT(ledClicked()));
    connect(ui->passwordButton, SIGNAL(clicked()), this, SLOT(showSetPasswordInfo()));
    connect(ui->seedButton, SIGNAL(clicked()), this, SLOT(seedHardwareWithName()));
    connect(ui->refreshButton, SIGNAL(clicked()), this, SLOT(SingleWalletUpdateWallets()));
    connect(ui->getNewAddress, SIGNAL(clicked()), this, SLOT(getNewAddress()));
    connect(ui->verifyAddressButton, SIGNAL(clicked()), this, SLOT(verifyAddress()));
    connect(ui->joinCopayWallet, SIGNAL(clicked()), this, SLOT(joinCopayWalletClicked()));
    connect(ui->checkProposals, SIGNAL(clicked()), this, SLOT(MultisigUpdateWallets()));
    connect(ui->showBackups, SIGNAL(clicked()), this, SLOT(showBackupDialog()));
    connect(ui->getRand, SIGNAL(clicked()), this, SLOT(getRandomNumber()));
    connect(ui->lockDevice, SIGNAL(clicked()), this, SLOT(lockDevice()));
    connect(ui->sendCoinsButton, SIGNAL(clicked()), this, SLOT(createTxProposalPressed()));
    connect(ui->getAddress, SIGNAL(clicked()), this, SLOT(showGetAddressDialog()));
    connect(ui->upgradeFirmware, SIGNAL(clicked()), this, SLOT(upgradeFirmwareButton()));
    connect(ui->openSettings, SIGNAL(clicked()), this, SLOT(showSettings()));
    connect(ui->pairDeviceButton, SIGNAL(clicked()), this, SLOT(pairSmartphone()));
    ui->upgradeFirmware->setVisible(true);
    ui->keypathLabel->setVisible(false);//hide keypath label for now (only tooptip)
    connect(ui->tableWidget, SIGNAL(doubleClicked(QModelIndex)),this,SLOT(historyShowTx(QModelIndex)));
    connect(ui->deviceNameLabel, SIGNAL(clicked()),this,SLOT(setDeviceNameClicked()));

#ifdef DBB_USE_MULTIMEDIA
    // connect QRCode Scanner
    // initiaize QRCode scanner
    if (DBBQRCodeScanner::availability())
    {
        ui->qrCodeButton->setEnabled(true);
    }
    else
        ui->qrCodeButton->setEnabled(false);

    connect(ui->qrCodeButton, SIGNAL(clicked()),this,SLOT(showQrCodeScanner()));
#else
    ui->qrCodeButton->setVisible(false);
#endif

    // connect custom signals
    connect(this, SIGNAL(XPubForCopayWalletIsAvailable(int)), this, SLOT(getRequestXPubKeyForCopay(int)));
    connect(this, SIGNAL(RequestXPubKeyForCopayWalletIsAvailable(int)), this, SLOT(joinCopayWallet(int)));
    connect(this, SIGNAL(gotResponse(const UniValue&, dbb_cmd_execution_status_t, dbb_response_type_t, int)), this, SLOT(parseResponse(const UniValue&, dbb_cmd_execution_status_t, dbb_response_type_t, int)));
    connect(this, SIGNAL(shouldVerifySigning(DBBWallet*, const UniValue&, int, const std::string&)), this, SLOT(showEchoVerification(DBBWallet*, const UniValue&, int, const std::string&)));
    connect(this, SIGNAL(shouldHideVerificationInfo()), this, SLOT(hideVerificationInfo()));
    connect(this, SIGNAL(signedProposalAvailable(DBBWallet*, const UniValue&, const std::vector<std::string>&)), this, SLOT(postSignaturesForPaymentProposal(DBBWallet*, const UniValue&, const std::vector<std::string>&)));
    connect(this, SIGNAL(getWalletsResponseAvailable(DBBWallet*, bool, const std::string&, bool)), this, SLOT(parseWalletsResponse(DBBWallet*, bool, const std::string&, bool)));
    connect(this, SIGNAL(getTransactionHistoryAvailable(DBBWallet*, bool, const UniValue&)), this, SLOT(updateTransactionTable(DBBWallet*, bool, const UniValue&)));

    connect(this, SIGNAL(shouldUpdateWallet(DBBWallet*)), this, SLOT(updateWallet(DBBWallet*)));
    connect(this, SIGNAL(walletAddressIsAvailable(DBBWallet*,const std::string &,const std::string &)), this, SLOT(updateReceivingAddress(DBBWallet*,const std::string&,const std::string &)));
    connect(this, SIGNAL(paymentProposalUpdated(DBBWallet*,const UniValue&)), this, SLOT(reportPaymentProposalPost(DBBWallet*,const UniValue&)));

    connect(this, SIGNAL(firmwareThreadDone(bool)), this, SLOT(upgradeFirmwareDone(bool)));
    connect(this, SIGNAL(shouldUpdateModalInfo(const QString&)), this, SLOT(updateModalInfo(const QString&)));
    connect(this, SIGNAL(shouldHideModalInfo()), this, SLOT(hideModalInfo()));

    connect(this, SIGNAL(createTxProposalDone(DBBWallet *, const QString&, const UniValue&)), this, SLOT(PaymentProposalAction(DBBWallet*,const QString&, const UniValue&)));
    connect(this, SIGNAL(shouldShowAlert(const QString&,const QString&)), this, SLOT(showAlert(const QString&,const QString&)));
    connect(this, SIGNAL(changeNetLoading(bool)), this, SLOT(setNetLoading(bool)));
    connect(this, SIGNAL(joinCopayWalletDone(DBBWallet*)), this, SLOT(joinCopayWalletComplete(DBBWallet*)));

    connect(this, SIGNAL(comServerIncommingMessage(const QString&)), this, SLOT(comServerMessageParse(const QString&)));

    // create backup dialog instance
    backupDialog = new BackupDialog(0);
    connect(backupDialog, SIGNAL(addBackup()), this, SLOT(addBackup()));
    connect(backupDialog, SIGNAL(verifyBackup(const QString&)), this, SLOT(verifyBackup(const QString&)));
    connect(backupDialog, SIGNAL(eraseAllBackups()), this, SLOT(eraseAllBackups()));
    connect(backupDialog, SIGNAL(eraseBackup(const QString&)), this, SLOT(eraseBackup(const QString&)));
    connect(backupDialog, SIGNAL(restoreFromBackup(const QString&)), this, SLOT(restoreBackup(const QString&)));

    // create get address dialog
    getAddressDialog = new GetAddressDialog(0);
    connect(getAddressDialog, SIGNAL(shouldGetXPub(const QString&)), this, SLOT(getAddressGetXPub(const QString&)));
    connect(getAddressDialog, SIGNAL(verifyGetAddress(const QString&)), this, SLOT(getAddressVerify(const QString&)));

    // connect direct reload lambda
    connect(this, &DBBDaemonGui::reloadGetinfo, this, [this]() {
        // use action as you wish
        resetInfos();
        getInfo();
    });

    //set window icon
    QApplication::setWindowIcon(QIcon(":/icons/dbb_icon"));
    //: translation: window title
    setWindowTitle(tr("Digital Bitbox") + (DBB_USE_TESTNET ? " ---TESTNET---" : ""));

    statusBar()->setStyleSheet("background: transparent;");
    this->statusBarButton = new QPushButton(QIcon(":/icons/connected"), "");
    this->statusBarButton->setEnabled(false);
    this->statusBarButton->setFlat(true);
    this->statusBarButton->setMaximumWidth(18);
    this->statusBarButton->setMaximumHeight(18);
    this->statusBarButton->setVisible(false);
    statusBar()->addWidget(this->statusBarButton);

    QIcon vDeviceIcon;
    vDeviceIcon.addPixmap(QPixmap(":/icons/devicephone"), QIcon::Normal);
    vDeviceIcon.addPixmap(QPixmap(":/icons/devicephone"), QIcon::Disabled);
    this->statusBarVDeviceIcon = new QPushButton(vDeviceIcon, "");
    this->statusBarVDeviceIcon->setEnabled(false);
    this->statusBarVDeviceIcon->setFlat(true);
    this->statusBarVDeviceIcon->setMaximumWidth(18);
    this->statusBarVDeviceIcon->setMaximumHeight(18);
    this->statusBarVDeviceIcon->setVisible(false);
    this->statusBarVDeviceIcon->setToolTip(tr("Verification Devices"));

    QIcon netActivityIcon;
    netActivityIcon.addPixmap(QPixmap(":/icons/netactivity"), QIcon::Normal);
    netActivityIcon.addPixmap(QPixmap(":/icons/netactivity"), QIcon::Disabled);
    this->statusBarNetIcon = new QPushButton(netActivityIcon, "");
    this->statusBarNetIcon->setEnabled(false);
    this->statusBarNetIcon->setFlat(true);
    this->statusBarNetIcon->setMaximumWidth(18);
    this->statusBarNetIcon->setMaximumHeight(18);
    this->statusBarNetIcon->setVisible(false);
    this->statusBarNetIcon->setToolTip(tr("Internet Activity Indicator"));

    QIcon usbActivityIcon;
    usbActivityIcon.addPixmap(QPixmap(":/icons/usbactivity"), QIcon::Normal);
    usbActivityIcon.addPixmap(QPixmap(":/icons/usbactivity"), QIcon::Disabled);
    this->statusBarUSBIcon = new QPushButton(usbActivityIcon, "");
    this->statusBarUSBIcon->setEnabled(false);
    this->statusBarUSBIcon->setFlat(true);
    this->statusBarUSBIcon->setMaximumWidth(18);
    this->statusBarUSBIcon->setMaximumHeight(18);
    this->statusBarUSBIcon->setVisible(false);
    this->statusBarUSBIcon->setToolTip(tr("USB Communication Activity Indicator"));

    //: translation: status bar info in case of no device has been found
    this->statusBarLabelLeft = new QLabel(tr("No device connected"));
    statusBar()->addWidget(this->statusBarLabelLeft);

    statusBar()->addPermanentWidget(this->statusBarNetIcon);
    statusBar()->addPermanentWidget(this->statusBarUSBIcon);
    statusBar()->addPermanentWidget(this->statusBarVDeviceIcon);
    if (!netActivityAnimation) {
        QGraphicsOpacityEffect* eff = new QGraphicsOpacityEffect(this);
        this->statusBarNetIcon->setGraphicsEffect(eff);

        netActivityAnimation = new QPropertyAnimation(eff, "opacity");

        netActivityAnimation->setDuration(1000);
        netActivityAnimation->setKeyValueAt(0, 0.3);
        netActivityAnimation->setKeyValueAt(0.5, 1.0);
        netActivityAnimation->setKeyValueAt(1, 0.3);
        netActivityAnimation->setEasingCurve(QEasingCurve::OutQuad);
        netActivityAnimation->setLoopCount(-1);
        netActivityAnimation->start(QAbstractAnimation::DeleteWhenStopped);
    }
    if (!usbActivityAnimation) {
        QGraphicsOpacityEffect* eff = new QGraphicsOpacityEffect(this);
        this->statusBarUSBIcon->setGraphicsEffect(eff);

        usbActivityAnimation = new QPropertyAnimation(eff, "opacity");

        usbActivityAnimation->setDuration(1000);
        usbActivityAnimation->setKeyValueAt(0, 0.3);
        usbActivityAnimation->setKeyValueAt(0.5, 1.0);
        usbActivityAnimation->setKeyValueAt(1, 0.3);
        usbActivityAnimation->setEasingCurve(QEasingCurve::OutQuad);
        usbActivityAnimation->setLoopCount(-1);
        usbActivityAnimation->start(QAbstractAnimation::DeleteWhenStopped);
    }
    if (!verificationActivityAnimation) {
        QGraphicsOpacityEffect* eff = new QGraphicsOpacityEffect(this);
        this->statusBarVDeviceIcon->setGraphicsEffect(eff);

        verificationActivityAnimation = new QPropertyAnimation(eff, "opacity");

        verificationActivityAnimation->setDuration(150);
        verificationActivityAnimation->setKeyValueAt(0, 1.0);
        verificationActivityAnimation->setKeyValueAt(0.5, 0.3);
        verificationActivityAnimation->setKeyValueAt(1, 1.0);
        verificationActivityAnimation->setLoopCount(8);
        delete eff;
    }

    // Set the tx fee tooltips
    this->ui->feeLevel->setItemData(0, "Confirms as soon as possible", Qt::ToolTipRole);
    this->ui->feeLevel->setItemData(1, "Confirms ~30 minutes", Qt::ToolTipRole);
    this->ui->feeLevel->setItemData(2, "Confirms ~1 hour", Qt::ToolTipRole);
    this->ui->feeLevel->setItemData(3, "Confirms ~4 hours", Qt::ToolTipRole);

    connect(this->ui->overviewButton, SIGNAL(clicked()), this, SLOT(mainOverviewButtonClicked()));
    connect(this->ui->multisigButton, SIGNAL(clicked()), this, SLOT(mainMultisigButtonClicked()));
    connect(this->ui->receiveButton, SIGNAL(clicked()), this, SLOT(mainReceiveButtonClicked()));
    connect(this->ui->sendButton, SIGNAL(clicked()), this, SLOT(mainSendButtonClicked()));
    connect(this->ui->mainSettingsButton, SIGNAL(clicked()), this, SLOT(mainSettingsButtonClicked()));

    //login screen setup
    this->ui->blockerView->setVisible(false);
    connect(this->ui->passwordLineEdit, SIGNAL(returnPressed()), this, SLOT(passwordProvided()));

    //set password screen
    connect(this->ui->modalBlockerView, SIGNAL(newPasswordAvailable(const QString&, const QString&)), this, SLOT(setPasswordProvided(const QString&, const QString&)));
    connect(this->ui->modalBlockerView, SIGNAL(newDeviceNamePasswordAvailable(const QString&, const QString&)), this, SLOT(setDeviceNamePasswordProvided(const QString&, const QString&)));
    connect(this->ui->modalBlockerView, SIGNAL(newDeviceNameAvailable(const QString&)), this, SLOT(setDeviceNameProvided(const QString&)));
    connect(this->ui->modalBlockerView, SIGNAL(signingShouldProceed(const QString&, void *, const UniValue&, int)), this, SLOT(proceedVerification(const QString&, void *, const UniValue&, int)));
    connect(this->ui->modalBlockerView, SIGNAL(shouldUpgradeFirmware()), this, SLOT(upgradeFirmwareButton()));
    //modal general signals
    connect(this->ui->modalBlockerView, SIGNAL(modalViewWillShowHide(bool)), this, SLOT(modalStateChanged(bool)));

    //get the default data dir
    std::string dataDir = DBB::GetDefaultDBBDataDir();

    //load the configuration file
    configData = new DBB::DBBConfigdata(dataDir+"/config.dat");
    configData->read();

    //create the single and multisig wallet
    singleWallet = new DBBWallet(dataDir, DBB_USE_TESTNET);
    singleWallet->setBaseKeypath(DBB::GetArg("-keypath", DBB_USE_TESTNET ? "m/44'/1'/0'" : "m/44'/0'/0'"));
    DBBWallet* copayWallet = new DBBWallet(dataDir, DBB_USE_TESTNET);
    copayWallet->setBaseKeypath(DBB::GetArg("-mskeypath","m/100'/45'/0'"));
    this->ui->noProposalsAvailable->setVisible(false);

#if defined(__linux__) || defined(__unix__)
    singleWallet->setCAFile(ca_file);
    copayWallet->setCAFile(ca_file);
    checkUDevRule();
#endif

    singleWallet->setSocks5ProxyURL(configData->getSocks5ProxyURL());
    copayWallet->setSocks5ProxyURL(configData->getSocks5ProxyURL());

    vMultisigWallets.push_back(copayWallet);
    updateSettings(); //update backends

    processCommand = false;
    deviceConnected = false;
    resetInfos();

    //set status bar connection status
    uiUpdateDeviceState();
    std::string devicePath;
    changeConnectedState(DBB::isConnectionOpen(), DBB::deviceAvailable(devicePath));

    //connect the device status update at very last point in init
    connect(this, SIGNAL(deviceStateHasChanged(bool, int)), this, SLOT(changeConnectedState(bool, int)));

    //connect to the com server
    comServer = new DBBComServer(configData->getComServerURL());
    comServer->setSocks5ProxyURL(configData->getSocks5ProxyURL());
#if defined(__linux__) || defined(__unix__)
    // set the CA file in case we are compliling for linux
    comServer->setCAFile(ca_file);
#endif
    comServer->setParseMessageCB(comServerCallback, this);
    if (configData->getComServerChannelID().size() > 0)
    {
        comServer->setChannelID(configData->getComServerChannelID());
        comServer->setEncryptionKey(configData->getComServerEncryptionKey());
        comServer->startLongPollThread();
        pingComServer();
    }

    walletUpdateTimer = new QTimer(this);
    connect(walletUpdateTimer, SIGNAL(timeout()), this, SLOT(updateTimerFired()));

    updateManager = new DBBUpdateManager();
    updateManager->setSocks5ProxyURL(configData->getSocks5ProxyURL());
#if defined(__linux__) || defined(__unix__)
    updateManager->setCAFile(ca_file);
#endif

    connect(ui->checkForUpdates, SIGNAL(clicked()), updateManager, SLOT(checkForUpdate()));
    connect(updateManager, SIGNAL(updateButtonSetAvailable(bool)), this, SLOT(updateButtonSetAvailable(bool)));
    QTimer::singleShot(200, updateManager, SLOT(checkForUpdateInBackground()));
}


void DBBDaemonGui::initTranslations() {
    char* dynamic = std::getenv("LANGFILE");
    if (dynamic != nullptr) {
        // Load translation file dynamically from a *.qm file to enable the translator to see the results.
        showAlert("Unsupported", "This is an debug version of the Digital Bitbox software. If this was not indended, please contact support.");
        if (translator.load(dynamic)) {
            QApplication::installTranslator(&translator);
        } else {
            showAlert("Error", "Language file not found (or it contains no translations)");
        }
        return;
    }
    // Otherwise, use the translations which are compiled into the binary.
    QString lang_territory = QLocale::system().name();
    QString lang = lang_territory;
    lang.truncate(lang_territory.lastIndexOf('_'));
    if (translator.load(lang, ":/translations/")) {
        QApplication::installTranslator(&translator);
    }
}

/*
 /////////////////////////////
 Plug / Unplug / GetInfo stack
 /////////////////////////////
*/
#pragma mark plug / unpluag stack

void DBBDaemonGui::deviceIsReadyToInteract()
{
    DBB::LogPrint("Device is ready to interact\n", "");
    //update multisig wallet data
    MultisigUpdateWallets();
    SingleWalletUpdateWallets();
    deviceReadyToInteract = true;

    if (singleWallet->client.IsSeeded())
        walletUpdateTimer->start(WALLET_POLL_TIME);
}

void DBBDaemonGui::changeConnectedState(bool state, int deviceType)
{
    bool stateChanged = deviceConnected != state;
    if (!state && deviceType != DBB::DBB_DEVICE_NO_DEVICE && deviceType != DBB::DBB_DEVICE_UNKNOWN)
        this->ui->noDeviceConnectedLabel->setText(tr("Device occupied by another program"));
    else
        this->ui->noDeviceConnectedLabel->setText(tr("No device connected"));

    // special case for firmware upgrades
    if (upgradeFirmwareState && stateChanged)
    {
        deviceConnected = state;

        if (deviceType == DBB::DBB_DEVICE_MODE_BOOTLOADER && state)
                upgradeFirmwareWithFile(firmwareFileToUse);
        return;
    }

    if (stateChanged) {
        if (state && (deviceType == DBB::DBB_DEVICE_MODE_FIRMWARE || deviceType == DBB::DBB_DEVICE_MODE_FIRMWARE_NO_PASSWORD || deviceType == DBB::DBB_DEVICE_MODE_FIRMWARE_U2F || deviceType == DBB::DBB_DEVICE_MODE_FIRMWARE_U2F_NO_PASSWORD)) {
            deviceConnected = true;
            DBB::LogPrint("Device connected\n", "");
            //: translation: device connected status bar
            this->statusBarLabelLeft->setText(tr("Device connected"));
            this->statusBarButton->setVisible(true);
        }
        else if (state && deviceType == DBB::DBB_DEVICE_MODE_BOOTLOADER && !upgradeFirmwareState) {
            // bricked, bootloader device found, ask for upgrading the firmware
            this->ui->noDeviceConnectedLabel->setText(tr("<b>The device is in bootloader mode.</b><br><br>To enter normal mode, replug the device and do not press the touch button. If a firmware upgrade errored, try upgrading again.<br><br><a href=\"up\">Upgrade now.</a>"));
            this->ui->noDeviceConnectedLabel->setWordWrap(true);
            this->ui->noDeviceIcon->setVisible(false);
        }
        else {
            deviceConnected = false;
            DBB::LogPrint("Device disconnected\n", "");
            this->statusBarLabelLeft->setText(tr("No device connected"));
            this->statusBarButton->setVisible(false);
            this->ui->noDeviceIcon->setVisible(true);
        }

        uiUpdateDeviceState(deviceType);
    }
}

void DBBDaemonGui::setTabbarEnabled(bool status)
{
    this->ui->menuBlocker->setVisible(!status);
    this->ui->overviewButton->setEnabled(status);
    this->ui->receiveButton->setEnabled(status);
    this->ui->sendButton->setEnabled(status);
    this->ui->mainSettingsButton->setEnabled(status);
    this->ui->multisigButton->setEnabled(status);
}

void DBBDaemonGui::setLoading(bool status)
{
    if (!status && touchButtonInfo)
    {
        hideModalInfo();
        touchButtonInfo = false;
    }

    this->statusBarUSBIcon->setVisible(status);

    this->ui->unlockingInfo->setVisible(status);
}

void DBBDaemonGui::setNetLoading(bool status)
{
    this->statusBarNetIcon->setVisible(status);
}

void DBBDaemonGui::resetInfos()
{
    //: translation: info text for version and device name during loading getinfo
    this->ui->versionLabel->setText(tr("loading info..."));
    this->ui->deviceNameLabel->setText(tr("loading info..."));

    updateOverviewFlags(false, false, true);

    //reset single wallet UI
    this->ui->tableWidget->setModel(NULL);
    this->ui->balanceLabel->setText("");
    this->ui->singleWalletBalance->setText("");
    this->ui->qrCode->setIcon(QIcon());
    this->ui->qrCode->setToolTip("");
    this->ui->keypathLabel->setText("");
    this->ui->currentAddress->setText("");

    //reset multisig wallet UI
    hidePaymentProposalsWidget();
    this->ui->noProposalsAvailable->setVisible(false);

}

void DBBDaemonGui::uiUpdateDeviceState(int deviceType)
{
    this->ui->verticalLayoutWidget->setVisible(deviceConnected);
    this->ui->balanceLabel->setVisible(deviceConnected);
    this->ui->noDeviceWidget->setVisible(!deviceConnected);

    if (!deviceConnected) {
        gotoOverviewPage();
        setActiveArrow(0);
        resetInfos();
        sessionPassword.clear();
        hideSessionPasswordView();
        setTabbarEnabled(false);
        deviceReadyToInteract = false;
        cachedWalletAvailableState = false;
        initialWalletSeeding = false;
        cachedDeviceLock = false;
        //hide modal dialog and abort possible ecdh pairing
        hideModalInfo();

        //clear some infos
        sdcardWarned = false;

        //remove the wallets
        singleWallet->client.setNull();
        vMultisigWallets[0]->client.setNull();

        if (walletUpdateTimer)
            walletUpdateTimer->stop();

        netLoaded = false;

    } else {
        if (deviceType == DBB::DBB_DEVICE_MODE_FIRMWARE || deviceType == DBB::DBB_DEVICE_MODE_FIRMWARE_U2F)
        {
            hideModalInfo();
            askForSessionPassword();
        }
        else if (deviceType == DBB::DBB_DEVICE_MODE_FIRMWARE_NO_PASSWORD || deviceType == DBB::DBB_DEVICE_MODE_FIRMWARE_U2F_NO_PASSWORD)
        {
            hideModalInfo();
            this->ui->modalBlockerView->showSetNewWallet();
        }
    }
}

void DBBDaemonGui::updateTimerFired()
{
    SingleWalletUpdateWallets(false);
    pingComServer();
}


void DBBDaemonGui::pingComServer()
{
    std::time_t now;
    std::time(&now);

    if (lastPing != 0 && lastPing+10 < now) {
        this->statusBarVDeviceIcon->setVisible(false);
        comServer->mobileAppConnected = false;
    }

    std::time(&lastPing);
    comServer->postNotification("{ \"action\" : \"ping\" }");
}
/*
 /////////////////
 UI Action Stack
 /////////////////
*/
#pragma mark UI Action Stack

void DBBDaemonGui::showAlert(const QString& title, const QString& errorOut, bool critical)
{
    if (critical)
        QMessageBox::critical(this, title, errorOut, QMessageBox::Ok);
    else
        QMessageBox::warning(this, title, errorOut, QMessageBox::Ok);
}

void DBBDaemonGui::setActiveArrow(int pos)
{
    this->ui->activeArrow->move(pos * 96 + 40, 39);
}

void DBBDaemonGui::mainOverviewButtonClicked()
{
    setActiveArrow(0);
    gotoOverviewPage();
}

void DBBDaemonGui::mainMultisigButtonClicked()
{
    setActiveArrow(4);
    gotoMultisigPage();
}

void DBBDaemonGui::mainReceiveButtonClicked()
{
    setActiveArrow(1);
    gotoReceivePage();
}

void DBBDaemonGui::mainSendButtonClicked()
{
    setActiveArrow(2);
    gotoSendCoinsPage();
}

void DBBDaemonGui::mainSettingsButtonClicked()
{
    setActiveArrow(3);
    gotoSettingsPage();
}

void DBBDaemonGui::gotoOverviewPage()
{
    this->ui->stackedWidget->setCurrentIndex(0);
}

void DBBDaemonGui::gotoSendCoinsPage()
{
    this->ui->stackedWidget->setCurrentIndex(3);
}

void DBBDaemonGui::gotoReceivePage()
{
    this->ui->stackedWidget->setCurrentIndex(4);
}

void DBBDaemonGui::gotoMultisigPage()
{
    this->ui->stackedWidget->setCurrentIndex(1);
}

void DBBDaemonGui::gotoSettingsPage()
{
    this->ui->stackedWidget->setCurrentIndex(2);
}

void DBBDaemonGui::showEchoVerification(DBBWallet* wallet, const UniValue& proposalData, int actionType, const std::string& echoStr)
{
    // check the required amount of steps
    std::vector<std::pair<std::string, std::vector<unsigned char> > > inputHashesAndPaths;
    std::string serTxDummy;
    UniValue changeAddressDataDummy;
    wallet->client.ParseTxProposal(proposalData, changeAddressDataDummy, serTxDummy, inputHashesAndPaths);
    int amountOfSteps = ceil((double)inputHashesAndPaths.size()/(double)MAX_INPUTS_PER_SIGN);
    int currentStep   = ceil((double)wallet->mapHashSig.size() /(double)MAX_INPUTS_PER_SIGN)+1;

    if (comServer->mobileAppConnected)
    {
        comServer->postNotification(echoStr);
        verificationActivityAnimation->start(QAbstractAnimation::KeepWhenStopped);
    }

    ui->modalBlockerView->setTXVerificationData(wallet, proposalData, echoStr, actionType);
    ui->modalBlockerView->showTransactionVerification(cachedDeviceLock, !comServer->mobileAppConnected, currentStep, amountOfSteps);

    if (!cachedDeviceLock)
    {
        //no follow up action required, clear TX data
        ui->modalBlockerView->clearTXData();

        //directly start DBB signing process
        PaymentProposalAction(wallet, "", proposalData, actionType);

    }
    else
        updateModalWithIconName(":/icons/touchhelp_smartverification");
}

void DBBDaemonGui::proceedVerification(const QString& twoFACode, void *ptr, const UniValue& proposalData, int actionType)
{
    if (twoFACode.isEmpty() && ptr == NULL)
    {
        //cancel pressed
        ui->modalBlockerView->clearTXData();
        hideModalInfo();
        ledClicked(DBB_LED_BLINK_MODE_ABORT);
        if (comServer)
            comServer->postNotification("{ \"action\" : \"clear\" }");
        return;
    }
    updateModalWithIconName(":/icons/touchhelp");
    DBBWallet *wallet = (DBBWallet *)ptr;
    PaymentProposalAction(wallet, twoFACode, proposalData, actionType);
    ui->modalBlockerView->clearTXData();
}

void DBBDaemonGui::hideVerificationInfo()
{
    if (signConfirmationDialog) {
        signConfirmationDialog->hide();
    }
}

void DBBDaemonGui::passwordProvided()
{
    if (sessionPassword.size() > 0)
        return;

    if (!loginScreenIndicatorOpacityAnimation) {
        QGraphicsOpacityEffect* eff = new QGraphicsOpacityEffect(this);
        this->ui->unlockingInfo->setGraphicsEffect(eff);

        loginScreenIndicatorOpacityAnimation = new QPropertyAnimation(eff, "opacity");

        loginScreenIndicatorOpacityAnimation->setDuration(500);
        loginScreenIndicatorOpacityAnimation->setKeyValueAt(0, 0.3);
        loginScreenIndicatorOpacityAnimation->setKeyValueAt(0.5, 1.0);
        loginScreenIndicatorOpacityAnimation->setKeyValueAt(1, 0.3);
        loginScreenIndicatorOpacityAnimation->setEasingCurve(QEasingCurve::OutQuad);
        loginScreenIndicatorOpacityAnimation->setLoopCount(-1);
    }

    // to slide in call
    loginScreenIndicatorOpacityAnimation->start(QAbstractAnimation::KeepWhenStopped);

    DBB::LogPrint("Storing session password in memory\n", "");
    sessionPassword = this->ui->passwordLineEdit->text().toStdString();

    DBB::LogPrint("Requesting device info...\n", "");
    getInfo();
}

void DBBDaemonGui::passwordAccepted()
{
    hideSessionPasswordView();
    this->ui->passwordLineEdit->setVisible(false);
    this->ui->passwordLineEdit->setText("");
    setTabbarEnabled(true);
}

void DBBDaemonGui::askForSessionPassword()
{
    setLoading(false);
    this->ui->blockerView->setVisible(true);
    this->ui->passwordLineEdit->setVisible(true);
    QWidget* slide = this->ui->blockerView;
    // setup slide
    slide->setGeometry(-slide->width(), 0, slide->width(), slide->height());

    // then a animation:
    QPropertyAnimation* animation = new QPropertyAnimation(slide, "pos");
    animation->setDuration(300);
    animation->setStartValue(slide->pos());
    animation->setEndValue(QPoint(0, 0));
    animation->setEasingCurve(QEasingCurve::OutQuad);
    // to slide in call
    animation->start(QAbstractAnimation::DeleteWhenStopped);
    ui->passwordLineEdit->setFocus();
}

void DBBDaemonGui::hideSessionPasswordView()
{
    this->ui->passwordLineEdit->setText("");

    if (loginScreenIndicatorOpacityAnimation)
        loginScreenIndicatorOpacityAnimation->stop();

    QWidget* slide = this->ui->blockerView;

    // then a animation:
    QPropertyAnimation* animation = new QPropertyAnimation(slide, "pos");
    animation->setDuration(300);
    animation->setStartValue(slide->pos());
    animation->setEndValue(QPoint(-slide->width(), 0));
    animation->setEasingCurve(QEasingCurve::OutQuad);
    // to slide in call
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void DBBDaemonGui::showSetPasswordInfo()
{
    QMessageBox::information(this, tr("Change Password"), tr("Changing the device password does NOT change the backup password. The backup password is ALWAYS the password that was used during wallet initialization. Be sure to remember it."), QMessageBox::Ok);
    ui->modalBlockerView->showSetPassword();
}

void DBBDaemonGui::setPasswordProvided(const QString& newPassword, const QString& repeatPassword)
{
    std::string command = "{\"password\" : \"" + newPassword.toStdString() + "\"}";

    if (repeatPassword.toStdString() != sessionPassword) {
        showModalInfo(tr("Incorrect old password"), DBB_PROCESS_INFOLAYER_CONFIRM_WITH_BUTTON);
        return;
    }

    showModalInfo(tr("Saving Password"));
    if (executeCommandWrapper(command, DBB_PROCESS_INFOLAYER_STYLE_TOUCHBUTTON, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_PASSWORD);
    }))
    {
        sessionPasswordDuringChangeProcess = sessionPassword;
        sessionPassword = newPassword.toStdString();
    }
}

void DBBDaemonGui::setDeviceNamePasswordProvided(const QString& newPassword, const QString& newName)
{
    tempNewDeviceName = newName;

    std::string command = "{\"password\" : \"" + newPassword.toStdString() + "\"}";
    showModalInfo(tr("Saving Password"));
    if (executeCommandWrapper(command, DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_PASSWORD);
    }))
    {
        sessionPasswordDuringChangeProcess = sessionPassword;
        sessionPassword = newPassword.toStdString();
    }
}

void DBBDaemonGui::cleanseLoginAndSetPassword()
{
    ui->modalBlockerView->cleanse();
    this->ui->passwordLineEdit->clear();
}

void DBBDaemonGui::showModalInfo(const QString &info, int helpType)
{
    ui->modalBlockerView->showModalInfo(info, helpType);
}

void DBBDaemonGui::updateModalInfo(const QString &info)
{
    ui->modalBlockerView->setText(info);
}

void DBBDaemonGui::hideModalInfo()
{
    ui->modalBlockerView->showOrHide(false);
}

void DBBDaemonGui::modalStateChanged(bool state)
{
    this->ui->sendToAddress->setEnabled(!state);
    this->ui->sendAmount->setEnabled(!state);
}

void DBBDaemonGui::updateModalWithQRCode(const QString& string)
{
    QRcode *code = QRcode_encodeString(string.toStdString().c_str(), 0, QR_ECLEVEL_M, QR_MODE_8, 1);
    QIcon icon;
    QRCodeSequence::setIconFromQRCode(code, &icon, 180, 180);
    ui->modalBlockerView->updateIcon(icon);
    QRcode_free(code);
}

void DBBDaemonGui::updateModalWithIconName(const QString& filename)
{
    QIcon newIcon;
    newIcon.addPixmap(QPixmap(filename), QIcon::Normal);
    newIcon.addPixmap(QPixmap(filename), QIcon::Disabled);
    ui->modalBlockerView->updateIcon(newIcon);
}

void DBBDaemonGui::updateOverviewFlags(bool walletAvailable, bool lockAvailable, bool loading)
{

}

void DBBDaemonGui::updateButtonSetAvailable(bool available)
{
    if (available) {
        this->ui->checkForUpdates->setText(tr("Update Available"));
        this->ui->checkForUpdates->setStyleSheet("font-weight: bold");
    }
    else {
        this->ui->checkForUpdates->setText(tr("Check for Updates..."));
        this->ui->checkForUpdates->setStyleSheet("font-weight: normal");
    }
}

/*
 //////////////////////////
 DBB USB Commands (General)
 //////////////////////////
*/
#pragma mark DBB USB Commands (General)

bool DBBDaemonGui::executeCommandWrapper(const std::string& cmd, const dbb_process_infolayer_style_t layerstyle, std::function<void(const std::string&, dbb_cmd_execution_status_t status)> cmdFinished, const QString& modaltext)
{
    if (processCommand)
        return false;

    if (layerstyle == DBB_PROCESS_INFOLAYER_STYLE_TOUCHBUTTON) {
        showModalInfo(modaltext, DBB_PROCESS_INFOLAYER_STYLE_TOUCHBUTTON);
        touchButtonInfo = true;
    }

    setLoading(true);
    processCommand = true;
    DBB::LogPrint("Executing command...\n", "");
    executeCommand(cmd, sessionPassword, cmdFinished);

    return true;
}

void DBBDaemonGui::eraseClicked()
{
    DBB::LogPrint("Request device erasing...\n", "");
    if (executeCommandWrapper("{\"reset\":\"__ERASE__\"}", DBB_PROCESS_INFOLAYER_STYLE_TOUCHBUTTON, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
            UniValue jsonOut;
            jsonOut.read(cmdOut);
            emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_ERASE);
        })) {

        sessionPasswordDuringChangeProcess = sessionPassword;
        sessionPassword.clear();
    }
}

void DBBDaemonGui::resetU2F()
{
    DBB::LogPrint("Request U2F reset...\n", "");
    if (executeCommandWrapper("{\"reset\":\"U2F\"}", DBB_PROCESS_INFOLAYER_STYLE_TOUCHBUTTON, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
            UniValue jsonOut;
            jsonOut.read(cmdOut);
            emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_RESET_U2F);
        })) {
    }
}

void DBBDaemonGui::ledClicked(dbb_led_blink_mode_t mode)
{
    std::string command;
    if (mode == DBB_LED_BLINK_MODE_BLINK)
        command = "{\"led\" : \"blink\"}";
    else if (mode == DBB_LED_BLINK_MODE_ABORT)
        command = "{\"led\" : \"abort\"}";
    else
        return;
    executeCommandWrapper(command, DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_LED_BLINK);
    });
}

void DBBDaemonGui::getInfo()
{
    executeCommandWrapper("{\"device\":\"info\"}", DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_INFO);
    });
}

std::string DBBDaemonGui::getBackupString()
{
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    // get device name
    std::string name = deviceName.toStdString();
    std::replace(name.begin(), name.end(), ' ', '_'); // default name has spaces, but spaces forbidden in backup file names

    std::stringstream ss;
    ss << name << "-" << DBB::putTime(in_time_t, "%Y-%m-%d-%H-%M-%S");
    return ss.str();
}

void DBBDaemonGui::seedHardwareWithName()
{
    ui->modalBlockerView->showSetDeviceNameCreate();
}

void DBBDaemonGui::seedHardware()
{
    if (sessionPassword.empty() || sessionPassword.size() > 64)
        return;

    std::string hashHex = DBB::getStretchedBackupHexKey(sessionPassword);

    DBB::LogPrint("Request device seeding...\n", "");
    std::string command = "{\"seed\" : {"
                                "\"source\" :\"create\","
                                "\"key\": \""+hashHex+"\","
                                "\"filename\": \"" + getBackupString() + ".pdf\""
                            "} }";

    executeCommandWrapper(command, (cachedWalletAvailableState) ? DBB_PROCESS_INFOLAYER_STYLE_TOUCHBUTTON : DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_CREATE_WALLET);
    }, "This will <strong>OVERWRITE</strong> your existing wallet with a new wallet.");
}

/*
/////////////////
DBB Utils
/////////////////
*/
#pragma mark DBB Utils

QString DBBDaemonGui::getIpAddress()
{
    QString ipAddress;
    QList<QString> possibleMatches;
    QList<QNetworkInterface> ifaces = QNetworkInterface::allInterfaces();
    if ( !ifaces.isEmpty() )
    {
        for(int i=0; i < ifaces.size(); i++)
        {
            unsigned int flags = ifaces[i].flags();
            bool isLoopback = (bool)(flags & QNetworkInterface::IsLoopBack);
            bool isP2P = (bool)(flags & QNetworkInterface::IsPointToPoint);
            bool isRunning = (bool)(flags & QNetworkInterface::IsRunning);

            // If this interface isn't running, we don't care about it
            if ( !isRunning ) continue;
            // We only want valid interfaces that aren't loopback/virtual and not point to point
            if ( !ifaces[i].isValid() || isLoopback || isP2P ) continue;
            QList<QHostAddress> addresses = ifaces[i].allAddresses();
            for(int a=0; a < addresses.size(); a++)
            {
                // Ignore local host
                if ( addresses[a] == QHostAddress::LocalHost ) continue;

                // Ignore non-ipv4 addresses
                if ( !addresses[a].toIPv4Address() ) continue;

                // Ignore self assigned IPs
                if ( addresses[a].isInSubnet(QHostAddress("169.254.0.0"), 16) )
                    continue;

                QString ip = addresses[a].toString();
                if ( ip.isEmpty() ) continue;
                bool foundMatch = false;
                for (int j=0; j < possibleMatches.size(); j++) if ( ip == possibleMatches[j] ) { foundMatch = true; break; }
                if ( !foundMatch ) { ipAddress = ip; break; }
            }
        }
    }

    return ipAddress;
}

void DBBDaemonGui::getRandomNumber()
{
    executeCommandWrapper("{\"random\" : \"pseudo\" }", DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_RANDOM_NUM);
    });
}

void DBBDaemonGui::lockDevice()
{
    QMessageBox::StandardButton reply = QMessageBox::question(this, "", tr("Do you have a backup?\nIs mobile app verification working?\n\n2FA mode DISABLES backups and mobile app pairing. The device must be ERASED to exit 2FA mode!\n\nProceed?"), QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::No)
        return;

    executeCommandWrapper("{\"device\" : \"lock\" }", DBB_PROCESS_INFOLAYER_STYLE_TOUCHBUTTON, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_DEVICE_LOCK);
    });
}

void DBBDaemonGui::lockBootloader()
{
    executeCommandWrapper("{\"bootloader\" : \"lock\" }", DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_BOOTLOADER_LOCK);
    });
}

void DBBDaemonGui::upgradeFirmwareButton()
{
    upgradeFirmware(true);
}

void DBBDaemonGui::upgradeFirmware(bool unlockbootloader)
{
    //: translation: Open file dialog help text
    firmwareFileToUse = QFileDialog::getOpenFileName(this, tr("Select Firmware"), "", tr("DBB Firmware Files (*.bin *.dbb)"));
    if (firmwareFileToUse.isNull())
        return;

    if (unlockbootloader)
    {
        DBB::LogPrint("Request bootloader unlock\n", "");
        executeCommandWrapper("{\"bootloader\" : \"unlock\" }", DBB_PROCESS_INFOLAYER_STYLE_TOUCHBUTTON, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
            UniValue jsonOut;
            jsonOut.read(cmdOut);
            emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_BOOTLOADER_UNLOCK);
        }, tr("Unlock the bootloader to install a new firmware"));
    }
    else
    {
        upgradeFirmwareWithFile(firmwareFileToUse);
    }
}

void DBBDaemonGui::noDeviceConnectedLabelLink(const QString& link)
{
    upgradeFirmware(false);
}

void DBBDaemonGui::upgradeFirmwareWithFile(const QString& fileName)
{
    if (!fileName.isNull())
    {
        std::string possibleFilename = fileName.toStdString();
        if (fwUpgradeThread)
        {
            fwUpgradeThread->join();
            delete fwUpgradeThread;
        }
        DBB::LogPrint("Start upgrading firmware\n", "");

        //: translation: started updating firmware info text
        showModalInfo(tr("<strong>Upgrading Firmware...</strong><br/><br/>Please stand by..."), DBB_PROCESS_INFOLAYER_STYLE_NO_INFO);

        setFirmwareUpdateHID(true);
        fwUpgradeThread = new std::thread([this,possibleFilename]() {
            bool upgradeRes = false;

            std::streamsize firmwareSize;
            std::stringstream buffer;

            if (possibleFilename.empty() || possibleFilename == "" || possibleFilename == "int")
            {
                // load internally
                for (int i = 0; i<firmware_deterministic_2_2_3_signed_bin_len;i++)
                {
                    buffer << firmware_deterministic_2_2_3_signed_bin[i];
                }
                firmwareSize = firmware_deterministic_2_2_3_signed_bin_len;
            }
            else {
                // load the file
                std::ifstream firmwareFile(possibleFilename, std::ios::binary);
                buffer << firmwareFile.rdbuf();
                firmwareSize = firmwareFile.tellg();
                firmwareFile.close();
            }

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
                    sigStr = DBB::dummySig(firmwareBuffer);
                }

                emit shouldUpdateModalInfo("<strong>Upgrading Firmware...</strong>");
                // send firmware blob to DBB
                if (DBB::upgradeFirmware(firmwareBuffer, firmwareSize, sigStr, [this](const std::string& infotext, float progress) {
                    emit shouldUpdateModalInfo(tr("<strong>Upgrading Firmware...</strong><br/><br/>%1% complete").arg(QString::number(progress*100, 'f', 1)));
                }))
                {
                    upgradeRes = true;
                }
            }
            emit firmwareThreadDone(upgradeRes);
        });
    }
    else
    {
        setFirmwareUpdateHID(false);
        upgradeFirmwareState = false;
    }
}

void DBBDaemonGui::upgradeFirmwareDone(bool status)
{
    setFirmwareUpdateHID(false);
    upgradeFirmwareState = false;
    hideModalInfo();
    deviceConnected = false;
    uiUpdateDeviceState();

    shouldKeepBootloaderState = false;

    if (status)
    {
        DBB::LogPrint("Firmware successfully upgraded\n", "");
        //: translation: successfull firmware update text
        showModalInfo(tr("<strong>Upgrade successful!</strong><br><br>Please unplug and replug your Digital Bitbox to continue. <br><font color=\"#6699cc\">Do not tap the touch button this time</font>."), DBB_PROCESS_INFOLAYER_STYLE_REPLUG);
    }
    else
    {
        DBB::LogPrint("Error while upgrading firmware\n", "");
        //: translation: firmware upgrade error
        showAlert(tr("Firmware Upgrade"), tr("Error while upgrading firmware. Please unplug and replug your Digital Bitbox."));
    }
}

void DBBDaemonGui::setDeviceNameProvided(const QString &name)
{
    setDeviceName(name, DBB_RESPONSE_TYPE_SET_DEVICE_NAME_RECREATE);
}

void DBBDaemonGui::setDeviceNameClicked()
{
    bool ok;
    QString tempDeviceName = QInputDialog::getText(this, "", tr("Enter device name"), QLineEdit::Normal, "", &ok);
    if (!ok || tempDeviceName.isEmpty())
        return;

    QRegExp nameMatcher("^[0-9A-Z-_ ]{1,64}$", Qt::CaseInsensitive);
    if (!nameMatcher.exactMatch(tempDeviceName))
    {
        showModalInfo(tr("The device name must only contain alphanumeric characters and - or _"), DBB_PROCESS_INFOLAYER_CONFIRM_WITH_BUTTON);
        return;
    }

    deviceName = tempDeviceName;
    setDeviceName(tempDeviceName, DBB_RESPONSE_TYPE_SET_DEVICE_NAME);
}

void DBBDaemonGui::setDeviceName(const QString &newDeviceName, dbb_response_type_t response_type)
{
    std::string command = "{\"name\" : \""+newDeviceName.toStdString()+"\" }";
    executeCommandWrapper(command, DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this, response_type](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, response_type);
    });
}

void DBBDaemonGui::parseBitcoinURI(const QString& uri, QString& addressOut, QString& amountOut)
{
    static const char bitcoinurl[] = "bitcoin:";
    static const char amountfield[] = "amount=";

    if (uri.startsWith(bitcoinurl, Qt::CaseInsensitive))
    {
        // get the part after the "bitcoin:"
        QString addressWithDetails = uri.mid(strlen(bitcoinurl));

        // form a substring with only the address
        QString onlyAddress = addressWithDetails.mid(0,addressWithDetails.indexOf("?"));

        // if there is an amount, rip our the string
        if (addressWithDetails.indexOf(amountfield) != -1)
        {
            QString part = addressWithDetails.mid(addressWithDetails.indexOf(amountfield));
            QString amount = part.mid(strlen(amountfield),part.indexOf("&")-strlen(amountfield));

            // fill amount
            amountOut = amount;
        }

        // fill address
        addressOut = onlyAddress;
    }
}

/*
////////////////////////
Address Exporting  Stack
////////////////////////
*/
#pragma mark Get Address Stack

void DBBDaemonGui::showGetAddressDialog()
{
    getAddressDialog->setBaseKeypath(singleWallet->baseKeypath());
    getAddressDialog->show();
}

void DBBDaemonGui::getAddressGetXPub(const QString& keypath)
{
    getXPub(keypath.toStdString(), DBB_RESPONSE_TYPE_XPUB_GET_ADDRESS, DBB_ADDRESS_STYLE_P2PKH);
}

void DBBDaemonGui::getAddressVerify(const QString& keypath)
{
    getXPub(keypath.toStdString(), DBB_RESPONSE_TYPE_XPUB_VERIFY, DBB_ADDRESS_STYLE_P2PKH);
}

/*
/////////////////
DBB Backup Stack
/////////////////
*/
#pragma mark DBB Backup

void DBBDaemonGui::showBackupDialog()
{
    backupDialog->show();
    listBackup();
}

void DBBDaemonGui::addBackup()
{
    std::string hashHex = DBB::getStretchedBackupHexKey(sessionPassword);
    std::string backupFilename = getBackupString();
    std::string command = "{\"backup\" : {\"encrypt\" :\"yes\","
                          "\"key\":\"" + hashHex + "\","
                          "\"filename\": \"" + backupFilename + ".pdf\"} }";

    DBB::LogPrint("Adding a backup (%s)\n", backupFilename.c_str());
    executeCommandWrapper(command, DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_ADD_BACKUP);
    });
}

void DBBDaemonGui::listBackup()
{
    std::string command = "{\"backup\" : \"list\" }";

    executeCommandWrapper(command, DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_LIST_BACKUP);
    });

    backupDialog->showLoading();
}

void DBBDaemonGui::eraseAllBackups()
{
    //: translation: Erase all backup warning text
    QMessageBox::StandardButton reply = QMessageBox::question(this, tr("Erase All Backups?"), tr("Are your sure you want to erase all backups"), QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::No)
        return;

    DBB::LogPrint("Erasing all backups...\n", "");
    std::string command = "{\"backup\" : \"erase\" }";

    executeCommandWrapper(command, DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_ERASE_BACKUP);
    });

    backupDialog->showLoading();
}

void DBBDaemonGui::verifyBackup(const QString& backupFilename)
{
    bool ok;
    QString tempBackupPassword = QInputDialog::getText(this, "", tr("Enter backup-file password"), QLineEdit::Password, "", &ok);
    if (!ok || tempBackupPassword.isEmpty())
        return;

    std::string hashHex = DBB::getStretchedBackupHexKey(tempBackupPassword.toStdString());
    std::string command = "{\"backup\" : {"
                "\"check\" :\"" + backupFilename.toStdString() + "\","
                "\"key\":\""+hashHex+"\""
                "} }";

    DBB::LogPrint("Verify single backup (%s)...\n", backupFilename.toStdString().c_str());
    executeCommandWrapper(command, DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_VERIFY_BACKUP, 1);
    });
}

void DBBDaemonGui::eraseBackup(const QString& backupFilename)
{
    //: translation: Erase all backup warning text
    QMessageBox::StandardButton reply = QMessageBox::question(this, tr("Erase Single Backup?"), tr("Are your sure you want to erase backup %1").arg(backupFilename), QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::No)
        return;

    std::string command = "{\"backup\" : { \"erase\" : \"" + backupFilename.toStdString() + "\" } }";

    DBB::LogPrint("Eraseing single backup (%s)...\n", backupFilename.toStdString().c_str());
    executeCommandWrapper(command, DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_ERASE_BACKUP, 1);
    });
}

void DBBDaemonGui::restoreBackup(const QString& backupFilename)
{
    bool ok;
    QString tempBackupPassword = QInputDialog::getText(this, "", tr("To restore a wallet from a backup, please enter the\ndevice password that was used during wallet initialization."), QLineEdit::Password, "", &ok);
    if (!ok || tempBackupPassword.isEmpty())
        return;

    std::string hashHex = DBB::getStretchedBackupHexKey(tempBackupPassword.toStdString());
    std::string command = "{\"seed\" : {"
                                "\"source\":\"backup\","
                                "\"filename\" :\"" + backupFilename.toStdString() + "\","
                                "\"key\":\""+hashHex+"\""
                            "} }";
    DBB::LogPrint("Restoring backup (%s)...\n", backupFilename.toStdString().c_str());
    executeCommandWrapper(command, (cachedWalletAvailableState) ? DBB_PROCESS_INFOLAYER_STYLE_TOUCHBUTTON : DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_CREATE_WALLET, 1);
    });

    backupDialog->close();
}

/*
 ///////////////////////////////////
 DBB USB Commands (Response Parsing)
 ///////////////////////////////////
*/
#pragma mark DBB USB Commands (Response Parsing)

void DBBDaemonGui::parseResponse(const UniValue& response, dbb_cmd_execution_status_t status, dbb_response_type_t tag, int subtag)
{
    DBB::LogPrint("Parsing response from device...\n", "");
    processCommand = false;
    setLoading(false);

    if (response.isObject()) {
        UniValue errorObj = find_value(response, "error");
        UniValue touchbuttonObj = find_value(response, "touchbutton");
        bool touchErrorShowed = false;

        if (touchbuttonObj.isStr()) {
            showAlert(tr("Touchbutton"), QString::fromStdString(touchbuttonObj.get_str()));
            touchErrorShowed = true;
        }

        bool errorShown = false;
        if (errorObj.isObject()) {
            //error found
            DBB::LogPrint("Got error object from device\n", "");
            //special case, password not accepted during "login" because of a ongoing-signing, etc.
            if (this->ui->blockerView->isVisible() && this->ui->passwordLineEdit->isVisible())
            {
                sessionPassword.clear();
                this->ui->passwordLineEdit->setText("");
                askForSessionPassword();
            }

            UniValue errorCodeObj = find_value(errorObj, "code");
            UniValue errorMessageObj = find_value(errorObj, "message");
            UniValue command = find_value(errorObj, "command");

            //hack to avoid backup verify/check error 108 result in a logout
            //remove it when MCU has different error code for that purpose
            if (errorCodeObj.isNum() && errorCodeObj.get_int() == 108 && (!command.isStr() || command.get_str() != "backup")) {
                //: translation: password wrong text
                showAlert(tr("Password Error"), tr("Password Wrong. %1").arg(QString::fromStdString(errorMessageObj.get_str())));
                errorShown = true;
                sessionPassword.clear();
                //try again
                askForSessionPassword();
            } else if (errorCodeObj.isNum() && errorCodeObj.get_int() == 110) {
                //: translation: password wrong device reset text
                showAlert(tr("Password Error"), tr("Device Reset. %1").arg(QString::fromStdString(errorMessageObj.get_str())), true);
                errorShown = true;
            } else if (errorCodeObj.isNum() && errorCodeObj.get_int() == 101) {
                sessionPassword.clear();
                this->ui->modalBlockerView->showSetNewWallet();
            } else {
                //password wrong
                showAlert(tr("Error"), QString::fromStdString(errorMessageObj.get_str()));
                errorShown = true;
            }
        } else if (tag == DBB_RESPONSE_TYPE_INFO) {
            DBB::LogPrint("Got device info\n", "");
            UniValue deviceObj = find_value(response, "device");
            if (deviceObj.isObject()) {
                UniValue version = find_value(deviceObj, "version");
                UniValue name = find_value(deviceObj, "name");
                UniValue seeded = find_value(deviceObj, "seeded");
                UniValue lock = find_value(deviceObj, "lock");
                UniValue sdcard = find_value(deviceObj, "sdcard");
                UniValue bootlock = find_value(deviceObj, "bootlock");
                UniValue walletIDUV = find_value(deviceObj, "id");
                cachedWalletAvailableState = seeded.isTrue();
                cachedDeviceLock = lock.isTrue();

                ui->lockDevice->setEnabled(!cachedDeviceLock);
                ui->seedButton->setEnabled(!cachedDeviceLock);
                ui->showBackups->setEnabled(!cachedDeviceLock);
                ui->pairDeviceButton->setEnabled(!cachedDeviceLock);
                if (cachedDeviceLock)
                    ui->lockDevice->setText(tr("Full 2FA is enabled"));
                else
                    ui->lockDevice->setText(tr("Enable Full 2FA"));

                //update version and check for compatibility
                if (version.isStr() && !DBB::mapArgs.count("-noversioncheck")) {
                    QString v = QString::fromStdString(version.get_str());
                    if (v.contains(QString("v1.")) || v.contains(QString("v0."))) {
                        showModalInfo(tr("Your Digital Bitbox uses <strong>old firmware incompatible with this app</strong>. Get the latest firmware at `digitalbitbox.com/firmware`. Upload it using the Options menu item Upgrade Firmware. Because the wallet key paths have changed, coins in a wallet created by an old app cannot be spent using the new app. You must use the old app to send coins to an address in a new wallet created by the new app.<br><br>To be safe, <strong>backup your old wallet</strong> before upgrading.<br>(Older firmware can be reloaded using the same procedure.)<br><br><br>"), DBB_PROCESS_INFOLAYER_UPGRADE_FIRMWARE);
                        return;
                    }
                    this->ui->versionLabel->setText(v);
                }

                try {
                    std::string vcopy = version.get_str();
                    vcopy.erase(std::remove(vcopy.begin(), vcopy.end(), 'v'), vcopy.end());
                    vcopy.erase(std::remove(vcopy.begin(), vcopy.end(), 'V'), vcopy.end());
                    vcopy.erase(std::remove(vcopy.begin(), vcopy.end(), '.'), vcopy.end());
                    int test = std::stoi(vcopy);
                    if (test < 220 && bootlock.isTrue()) {

                        QMessageBox msgBox;
                        msgBox.setText(tr("Update Firmware"));
                        msgBox.setInformativeText(tr("A firmware upgrade (%1) is available for your device. Do you wish to install it?").arg(QString::fromStdString(std::string(firmware_deterministic_string))));
                        QAbstractButton *showOnline = msgBox.addButton(tr("Show online information"), QMessageBox::RejectRole);
                        msgBox.addButton(QMessageBox::Yes);
                        msgBox.addButton(QMessageBox::No);
                        int res = msgBox.exec();
                        if (msgBox.clickedButton() == showOnline)
                        {
                            QDesktopServices::openUrl(QUrl("https://digitalbitbox.com/firmware?app=dbb-app"));
                        }
                        else if (res == QMessageBox::Yes) {
                            DBB::LogPrint("Upgrading firmware\n", "");
                            firmwareFileToUse = "int";
                            DBB::LogPrint("Request bootloader unlock\n", "");
                            executeCommandWrapper("{\"bootloader\" : \"unlock\" }", DBB_PROCESS_INFOLAYER_STYLE_TOUCHBUTTON, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
                                UniValue jsonOut;
                                jsonOut.read(cmdOut);
                                UniValue errorObj = find_value(jsonOut, "error");
                                if (errorObj.isObject()) {
                                    processCommand = false;
                                    emit shouldHideModalInfo();
                                    emit reloadGetinfo();
                                }
                                else {
                                    emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_BOOTLOADER_UNLOCK);
                                }
                            }, tr("Unlock the bootloader to install a new firmware"));
                            passwordAccepted();
                            return;
                        }
                    }
                } catch (std::exception &e) {

                }

                //update device name
                if (name.isStr())
                {
                    deviceName = QString::fromStdString(name.get_str());
                    this->ui->deviceNameLabel->setText("<strong>" + tr("Name:") + "</strong> " + deviceName);
                }

                this->ui->DBBAppVersion->setText("v"+QString(DBB_PACKAGE_VERSION));

                updateOverviewFlags(cachedWalletAvailableState, cachedDeviceLock, false);

                bool shouldCreateWallet = false;
                if (cachedWalletAvailableState && walletIDUV.isStr())
                {
                    //initializes wallets (filename, get address, etc.)
                    if (singleWallet->client.getFilenameBase().empty())
                    {
                        singleWallet->client.setFilenameBase(walletIDUV.get_str()+"_copay_single");
                        singleWallet->client.LoadLocalData();
                        std::string lastAddress, keypath;
                        singleWallet->client.GetLastKnownAddress(lastAddress, keypath);
                        singleWallet->rewriteKeypath(keypath);
                        updateReceivingAddress(singleWallet, lastAddress, keypath);

                        if (singleWallet->client.GetXPubKey().size() <= 0)
                            shouldCreateWallet = true;
                    }
                    if (vMultisigWallets[0]->client.getFilenameBase().empty())
                    {
                        vMultisigWallets[0]->client.setFilenameBase(walletIDUV.get_str()+"_copay_multisig_0");
                        vMultisigWallets[0]->client.LoadLocalData();
                    }
                }
                if (shouldCreateWallet)
                {
                    //: translation: modal info during copay wallet creation
                    showModalInfo(tr("Creating Wallet"));
                    createSingleWallet();
                }

                //enable UI
                passwordAccepted();

                if (!cachedWalletAvailableState)
                {
                    if (sdcard.isBool() && !sdcard.isTrue())
                    {
                        //: translation: warning text if micro SD card needs to be inserted for wallet creation
                        showModalInfo(tr("Please insert a micro SD card and <strong>replug</strong> the device. Make sure the SDCard is pushed in all the way. Initializing the wallet is only possible with an SD card. Otherwise, you will not have a backup."));
                        updateModalWithIconName(":/icons/touchhelp_sdcard_in");
                        return;
                    }
                    //: translation: modal text during seed command DBB
                    showModalInfo(tr("Creating Wallet"));
                    initialWalletSeeding = true;
                    seedHardware();
                    return;
                }

                deviceIsReadyToInteract();

                if (sdcard.isBool() && sdcard.isTrue() && cachedWalletAvailableState && !sdcardWarned)
                {
                    //: translation: warning text if micro SD card is inserted
                    showModalInfo(tr("Keep the SD card somewhere safe unless doing backups or restores."), DBB_PROCESS_INFOLAYER_CONFIRM_WITH_BUTTON);
                    updateModalWithIconName(":/icons/touchhelp_sdcard");

                    sdcardWarned = true;
                }

                // special case for post firmware upgrades (lock bootloader)
                if (!shouldKeepBootloaderState && bootlock.isFalse())
                {
                    // unlock bootloader
                    //: translation: modal infotext for guiding user to lock the bootloader
                    showModalInfo(tr("<strong>Lock Firmware...</strong><br/><br/>This prevents further unintentional firmware upgrades."), DBB_PROCESS_INFOLAYER_STYLE_TOUCHBUTTON);
                    lockBootloader();
                    shouldKeepBootloaderState = false;
                    return;
                }

                if (initialWalletSeeding)
                {
                    showModalInfo(tr(""), DBB_PROCESS_INFOLAYER_CONFIRM_WITH_BUTTON);
                    updateModalWithIconName(":/icons/touchhelp_initdone");
                    initialWalletSeeding = false;
                }

                if (openedWithBitcoinURI)
                {
                    QString amount;
                    QString address;
                    parseBitcoinURI(*openedWithBitcoinURI, address, amount);
                    delete openedWithBitcoinURI; openedWithBitcoinURI = NULL;
                    mainSendButtonClicked();

                    this->ui->sendToAddress->setText(address);
                    this->ui->sendAmount->setText(amount);
                }
            }
        } else if (tag == DBB_RESPONSE_TYPE_XPUB_MS_MASTER) {
            UniValue xPubKeyUV = find_value(response, "xpub");
            QString errorString;

            if (!xPubKeyUV.isNull() && xPubKeyUV.isStr()) {
                std::string xPubKeyNew;
                if (DBB_USE_TESTNET)
                {
                    btc_hdnode node;
                    bool r = btc_hdnode_deserialize(xPubKeyUV.get_str().c_str(), &btc_chain_main, &node);

                    char outbuf[112];
                    btc_hdnode_serialize_public(&node, &btc_chain_test, outbuf, sizeof(outbuf));

                    xPubKeyNew.assign(outbuf);
                }
                else
                    xPubKeyNew = xPubKeyUV.get_str();

                //0 = singlewallet
                if (subtag == 0)
                    singleWallet->client.setMasterPubKey(xPubKeyNew);
                else
                    vMultisigWallets[0]->client.setMasterPubKey(xPubKeyNew);

                emit XPubForCopayWalletIsAvailable(subtag);
            } else {
                if (xPubKeyUV.isObject()) {
                    UniValue errorObj = find_value(xPubKeyUV, "error");
                    if (!errorObj.isNull() && errorObj.isStr())
                        errorString = QString::fromStdString(errorObj.get_str());
                }

                showAlert(tr("Join Wallet Error"), tr("Error joining Copay Wallet (%1)").arg(errorString));
            }
        } else if (tag == DBB_RESPONSE_TYPE_XPUB_MS_REQUEST) {
            UniValue requestXPubKeyUV = find_value(response, "xpub");
            QString errorString;

            if (!requestXPubKeyUV.isNull() && requestXPubKeyUV.isStr()) {
                std::string xRequestKeyNew;
                if (DBB_USE_TESTNET)
                {
                    btc_hdnode node;
                    bool r = btc_hdnode_deserialize(requestXPubKeyUV.get_str().c_str(), &btc_chain_main, &node);

                    char outbuf[112];
                    btc_hdnode_serialize_public(&node, &btc_chain_test, outbuf, sizeof(outbuf));

                    xRequestKeyNew.assign(outbuf);
                }
                else
                    xRequestKeyNew = requestXPubKeyUV.get_str();

                if (subtag == 0)
                    singleWallet->client.setRequestPubKey(xRequestKeyNew);
                else
                    vMultisigWallets[0]->client.setRequestPubKey(xRequestKeyNew);

                emit RequestXPubKeyForCopayWalletIsAvailable(subtag);
            } else {
                if (requestXPubKeyUV.isObject()) {
                    UniValue errorObj = find_value(requestXPubKeyUV, "error");
                    if (!errorObj.isNull() && errorObj.isStr())
                        errorString = QString::fromStdString(errorObj.get_str());
                }

                showAlert(tr("Join Wallet Error"), tr("Error joining Copay Wallet (%1)").arg(errorString));
            }
        } else if (tag == DBB_RESPONSE_TYPE_XPUB_VERIFY) {
            UniValue responseMutable(UniValue::VOBJ);
            UniValue requestXPubKeyUV = find_value(response, "xpub");
            UniValue requestXPubEchoUV = find_value(response, "echo");
            QString errorString;
            if (requestXPubKeyUV.isStr() && requestXPubEchoUV.isStr()) {
                //pass the response to the verification devices
                responseMutable.pushKV("echo", requestXPubEchoUV.get_str().c_str());
                if (subtag == DBB_ADDRESS_STYLE_MULTISIG_1OF1)
                    responseMutable.pushKV("type", "p2sh_ms_1of1");
                if (subtag == DBB_ADDRESS_STYLE_P2PKH) {
                    responseMutable.pushKV("type", "p2pkh");

                    // compare the addresses
                    btc_hdnode node;
                    bool r = btc_hdnode_deserialize(requestXPubKeyUV.get_str().c_str(), &btc_chain_main, &node);
                    char address[1024];
                    btc_hdnode_get_p2pkh_address(&node, (DBB_USE_TESTNET ? &btc_chain_test : &btc_chain_main), address, sizeof(address));
                    std::string strAddress(address);
                    if (strAddress != ui->currentAddress->text().toStdString()) {
                        showModalInfo(tr("The address does <strong><font color=\"#AA0000\">not match</font></strong>."), DBB_PROCESS_INFOLAYER_CONFIRM_WITH_BUTTON);
                    }
                }
                // send verification to verification devices
                if (comServer->mobileAppConnected)
                    comServer->postNotification(responseMutable.write());
            }
            if (!comServer->mobileAppConnected)
            {
                if (!verificationDialog)
                    verificationDialog = new VerificationDialog();

                verificationDialog->show();
                verificationDialog->setData(tr("Securely Verify Your Receiving Address"), tr("No mobile app detected.<br><br>You can verify the address by manually scanning QR codes with a paired Digital Bitbox mobile app."), responseMutable.write());
            }
        } else if (tag == DBB_RESPONSE_TYPE_LIST_BACKUP && backupDialog) {
            UniValue backupObj = find_value(response, "backup");
            if (backupObj.isArray()) {
                std::vector<std::string> data;
                for (const UniValue& obj : backupObj.getValues())
                {
                    data.push_back(obj.get_str());
                }
                backupDialog->showList(data);
            }
        } else if (tag == DBB_RESPONSE_TYPE_ADD_BACKUP && backupDialog) {
            listBackup();
        } else if (tag == DBB_RESPONSE_TYPE_VERIFY_BACKUP && backupDialog) {
            UniValue verifyResult = find_value(response, "backup");
            if (verifyResult.isStr() && verifyResult.get_str() == "success") {
                showModalInfo(tr("This backup is a <strong><font color=\"#00AA00\">valid</font></strong> backup of your current wallet."), DBB_PROCESS_INFOLAYER_CONFIRM_WITH_BUTTON);
            }
            else
            {
                showModalInfo(tr("This backup does <strong><font color=\"#AA0000\">not match</font></strong> your current wallet."), DBB_PROCESS_INFOLAYER_CONFIRM_WITH_BUTTON);
            }
            backupDialog->hide();
        } else if (tag == DBB_RESPONSE_TYPE_ERASE_BACKUP && backupDialog) {
            listBackup();
        } else if (tag == DBB_RESPONSE_TYPE_RANDOM_NUM) {
            UniValue randomNumObjUV = find_value(response, "random");
            UniValue randomNumEchoUV = find_value(response, "echo");
            if (randomNumObjUV.isStr()) {
                showModalInfo("<strong>"+tr("Random hexadecimal number")+"</strong><br /><br />"+QString::fromStdString(randomNumObjUV.get_str()+""), DBB_PROCESS_INFOLAYER_CONFIRM_WITH_BUTTON);
                QString errorString;
                // send verification to verification devices
                if (randomNumEchoUV.isStr() && comServer->mobileAppConnected)
                    comServer->postNotification(response.write());
            }
        } else if (tag == DBB_RESPONSE_TYPE_DEVICE_LOCK) {
            bool suc = false;

            //check device:lock response and give appropriate user response
            UniValue deviceObj = find_value(response, "device");
            if (deviceObj.isObject()) {
                UniValue lockObj = find_value(deviceObj, "lock");
                if (lockObj.isBool() && lockObj.isTrue())
                    suc = true;
            }
            if (suc)
                QMessageBox::information(this, tr("Success"), tr("Your device is now locked"), QMessageBox::Ok);
            else
                showAlert(tr("Error"), tr("Could not lock your device"));

            //reload device infos
            resetInfos();
            getInfo();
        } else if (tag == DBB_RESPONSE_TYPE_BOOTLOADER_UNLOCK) {
            upgradeFirmwareState = true;
            shouldKeepBootloaderState = true;
            showModalInfo("<strong>Upgrading Firmware...</strong><br/><br/>Please unplug and replug your Digital Bitbox.<br>Before the LED turns off, briefly <font color=\"#6699cc\">tap the touch button</font> to start the upgrade.", DBB_PROCESS_INFOLAYER_STYLE_REPLUG);
        }
        else if (tag == DBB_RESPONSE_TYPE_BOOTLOADER_LOCK) {
            hideModalInfo();
        }
        else if (tag == DBB_RESPONSE_TYPE_SET_DEVICE_NAME || tag == DBB_RESPONSE_TYPE_SET_DEVICE_NAME_CREATE || tag == DBB_RESPONSE_TYPE_SET_DEVICE_NAME_RECREATE) {
            UniValue name = find_value(response, "name");
            if (name.isStr()) {
                deviceName = QString::fromStdString(name.get_str());
                this->ui->deviceNameLabel->setText("<strong>" + tr("Name:") + "</strong> " + deviceName);
                if (tag == DBB_RESPONSE_TYPE_SET_DEVICE_NAME_CREATE)
                    getInfo();
                if (tag == DBB_RESPONSE_TYPE_SET_DEVICE_NAME_RECREATE)
                    seedHardware();
            }
        }
        else {
        }

        //no else if because we want to hide the blocker view in case of an error
        if (tag == DBB_RESPONSE_TYPE_VERIFYPASS_ECDH)
        {
            if (errorObj.isObject()) {
                showAlert(tr("Error"), tr("Verification Device Pairing Failed"));
            }
            hideModalInfo();
            if (comServer)
                comServer->postNotification(response.write());
        }

        if (tag == DBB_RESPONSE_TYPE_CREATE_WALLET) {
            hideModalInfo();
            UniValue touchbuttonObj = find_value(response, "touchbutton");
            UniValue seedObj = find_value(response, "seed");
            UniValue errorObj = find_value(response, "error");
            QString errorString;

            if (errorObj.isObject()) {
                UniValue errorMsgObj = find_value(errorObj, "message");
                if (errorMsgObj.isStr())
                    errorString = QString::fromStdString(errorMsgObj.get_str());
            }
            if (!touchbuttonObj.isNull() && touchbuttonObj.isObject()) {
                UniValue errorObj = find_value(touchbuttonObj, "error");
                if (!errorObj.isNull() && errorObj.isStr())
                    errorString = QString::fromStdString(errorObj.get_str());
            }
            if (!seedObj.isNull() && seedObj.isStr() && seedObj.get_str() == "success") {
                // clear wallet information
                if (singleWallet)
                    singleWallet->client.setNull();

                if (vMultisigWallets[0])
                    vMultisigWallets[0]->client.setNull();

                resetInfos();
                getInfo();
            }
        }
        if (tag == DBB_RESPONSE_TYPE_ERASE) {
            UniValue resetObj = find_value(response, "reset");
            if (resetObj.isStr() && resetObj.get_str() == "success") {
                sessionPasswordDuringChangeProcess.clear();

                //remove local wallets
                singleWallet->client.setNull();
                vMultisigWallets[0]->client.setNull();

                resetInfos();
                getInfo();
            } else {
                //reset password in case of an error
                sessionPassword = sessionPasswordDuringChangeProcess;
                sessionPasswordDuringChangeProcess.clear();
            }
        }
        if (tag == DBB_RESPONSE_TYPE_XPUB_GET_ADDRESS) {
            getAddressDialog->updateAddress(response);
        }
        if (tag == DBB_RESPONSE_TYPE_PASSWORD) {
            UniValue passwordObj = find_value(response, "password");
            if (status != DBB_CMD_EXECUTION_STATUS_OK || (passwordObj.isStr() && passwordObj.get_str() == "success")) {
                sessionPasswordDuringChangeProcess.clear();
                cleanseLoginAndSetPassword(); //remove text from set password fields
                //could not decrypt, password was changed successfully
                setDeviceName(tempNewDeviceName, DBB_RESPONSE_TYPE_SET_DEVICE_NAME_CREATE);
                tempNewDeviceName = "";
            } else {
                QString errorString;
                UniValue touchbuttonObj = find_value(response, "touchbutton");
                if (!touchbuttonObj.isNull() && touchbuttonObj.isObject()) {
                    UniValue errorObj = find_value(touchbuttonObj, "error");
                    if (!errorObj.isNull() && errorObj.isStr())
                        errorString = QString::fromStdString(errorObj.get_str());
                }

                //reset password in case of an error
                sessionPassword = sessionPasswordDuringChangeProcess;
                sessionPasswordDuringChangeProcess.clear();

                if (!errorShown)
                {
                    //: translation: error text during password set (DBB)
                    showAlert(tr("Password Error"), tr("Could not set password (error: %1)!").arg(errorString));
                    errorShown = true;
                }
            }
        }
    }
    else {
        DBB::LogPrint("Parsing was invalid JSON\n", "");
    }
}

/*
/////////////////
copay single- and multisig wallet stack
/////////////////
*/
#pragma mark copay single- and multisig wallet stack

void DBBDaemonGui::createSingleWallet()
{
    if (!singleWallet->client.IsSeeded()) {
        getXPubKeyForCopay(0);
    } else {
        SingleWalletUpdateWallets();
    }
}

void DBBDaemonGui::getNewAddress()
{
    if (singleWallet->client.IsSeeded()) {
        DBBNetThread* thread = DBBNetThread::DetachThread();
        thread->currentThread = std::thread([this, thread]() {
            std::string walletsResponse;

            std::string address;
            std::string keypath;

            std::string error;
            singleWallet->client.GetNewAddress(address, keypath, error);
            if (address.size())
            {
                singleWallet->rewriteKeypath(keypath);
                emit walletAddressIsAvailable(this->singleWallet, address, keypath);
            }
            else
            {
                emit shouldShowAlert("Error", (error.size() > 1) ? QString::fromStdString(error) : tr("Could not get a new receiving address."));
                setNetLoading(false);
            }

            thread->completed();
        });

        setNetLoading(true);
    }
}

void DBBDaemonGui::verifyAddress()
{
    getXPub(ui->keypathLabel->text().toStdString(), DBB_RESPONSE_TYPE_XPUB_VERIFY, DBB_ADDRESS_STYLE_P2PKH);
}

void DBBDaemonGui::getXPub(const std::string& keypath,  dbb_response_type_t response_type, dbb_address_style_t address_type)
{
    executeCommandWrapper("{\"xpub\":\"" + keypath + "\"}", DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this,response_type,address_type](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, response_type, address_type);
    });
}

void DBBDaemonGui::updateReceivingAddress(DBBWallet *wallet, const std::string &newAddress, const std::string &info)
{
    setNetLoading(false);
    this->ui->currentAddress->setText(QString::fromStdString(newAddress));

    if (newAddress.size() <= 0)
        return;

    std::string uri = "bitcoin:"+newAddress;

    QRcode *code = QRcode_encodeString(uri.c_str(), 0, QR_ECLEVEL_M, QR_MODE_8, 1);
    if (code)
    {

        QImage myImage = QImage(code->width + 8, code->width + 8, QImage::Format_RGB32);
        myImage.fill(0xffffff);
        unsigned char *p = code->data;
        for (int y = 0; y < code->width; y++)
        {
            for (int x = 0; x < code->width; x++)
            {
                myImage.setPixel(x + 4, y + 4, ((*p & 1) ? 0x0 : 0xffffff));
                p++;
            }
        }
        QRcode_free(code);

        QIcon qrCode;
        QPixmap pixMap = QPixmap::fromImage(myImage).scaled(240, 240);
        qrCode.addPixmap(pixMap, QIcon::Normal);
        qrCode.addPixmap(pixMap, QIcon::Disabled);
        ui->qrCode->setIcon(qrCode);
    }

    ui->qrCode->setToolTip(QString::fromStdString(info));
    ui->keypathLabel->setText(QString::fromStdString(info));
}

void DBBDaemonGui::createTxProposalPressed()
{
    if (!singleWallet->client.IsSeeded())
        return;

    int64_t amount = 0;
    if (this->ui->sendAmount->text().size() == 0 || !DBB::ParseMoney(this->ui->sendAmount->text().toStdString(), amount))
        return showAlert(tr("Error"), tr("Invalid amount"));

    if (cachedDeviceLock && !comServer->mobileAppConnected)
        return showAlert(tr("Error"), tr("2FA enabled but no mobile app found online"));

    this->ui->sendToAddress->clearFocus();
    this->ui->sendAmount->clearFocus();
    DBBNetThread* thread = DBBNetThread::DetachThread();
    thread->currentThread = std::thread([this, thread, amount]() {
        UniValue proposalOut;
        std::string errorOut;

        int64_t fee = singleWallet->client.GetFeeForPriority(this->ui->feeLevel->currentIndex());
        if (fee == 0) {
            emit changeNetLoading(false);
            emit shouldHideModalInfo();
            emit shouldShowAlert("Error", tr("Could not estimate fees. Make sure you are online."));
        } else {
            if (!singleWallet->client.CreatePaymentProposal(this->ui->sendToAddress->text().toStdString(), amount, fee, proposalOut, errorOut)) {
                emit changeNetLoading(false);
                emit shouldHideModalInfo();
                emit shouldShowAlert("Error", QString::fromStdString(errorOut));
            }
            else
            {
                emit changeNetLoading(false);
                emit shouldUpdateModalInfo(tr("Start Signing Process"));
                emit createTxProposalDone(singleWallet, "", proposalOut);
            }
        }

        thread->completed();
    });
    setNetLoading(true);
    showModalInfo(tr("Creating Transaction"));
}

void DBBDaemonGui::reportPaymentProposalPost(DBBWallet* wallet, const UniValue& proposal)
{
    showModalInfo(tr("Transaction was sent successfully"), DBB_PROCESS_INFOLAYER_CONFIRM_WITH_BUTTON);
    //QMessageBox::information(this, tr("Success"), tr("Transaction was sent successfully"), QMessageBox::Ok);
}

void DBBDaemonGui::joinCopayWalletClicked()
{
    if (!vMultisigWallets[0]->client.IsSeeded()) {
        //if there is no xpub and request key, seed over DBB
        getXPubKeyForCopay(1);
    } else {
        //send a join request
        joinMultisigWalletInitiate(vMultisigWallets[0]);
    }
}

void DBBDaemonGui::joinMultisigWalletInitiate(DBBWallet* wallet)
{
    bool ok;
    QString text = QInputDialog::getText(this, tr("Join Copay Wallet"), tr("Wallet Invitation Code"), QLineEdit::Normal, "", &ok);
    if (!ok || text.isEmpty())
        return;

    // parse invitation code
    BitpayWalletInvitation invitation;
    if (!wallet->client.ParseWalletInvitation(text.toStdString(), invitation)) {
        showAlert(tr("Invalid Invitation"), tr("Your Copay wallet invitation is invalid"));
        return;
    }

    std::string result;
    bool ret = wallet->client.JoinWallet(wallet->participationName, invitation, result);

    if (!ret) {
        UniValue responseJSON;
        std::string additionalErrorText = "unknown";
        if (responseJSON.read(result)) {
            UniValue errorText;
            errorText = find_value(responseJSON, "message");
            if (!errorText.isNull() && errorText.isStr())
                additionalErrorText = errorText.get_str();
        }

        showAlert(tr("Copay Wallet Response"), tr("Joining the wallet failed (%1)").arg(QString::fromStdString(additionalErrorText)));
    } else {
        QMessageBox::information(this, tr("Copay Wallet Response"), tr("Successfully joined Copay wallet"), QMessageBox::Ok);
        wallet->client.walletJoined = true;
        wallet->client.SaveLocalData();
        MultisigUpdateWallets();
    }
}

void DBBDaemonGui::getXPubKeyForCopay(int walletIndex)
{
    DBBWallet* wallet = vMultisigWallets[0];
    if (walletIndex == 0)
        wallet = singleWallet;

    std::string baseKeyPath = wallet->baseKeypath();
    executeCommandWrapper("{\"xpub\":\"" + baseKeyPath + "\"}", DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this, walletIndex](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);

        //TODO: fix hack
        if (walletIndex == 0)
        {
            // small UI delay that people can read "Creating Wallet" modal screen
            std::this_thread::sleep_for(std::chrono::milliseconds(350));
        }
        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_XPUB_MS_MASTER, walletIndex);
    });
}

void DBBDaemonGui::getRequestXPubKeyForCopay(int walletIndex)
{
    DBBWallet* wallet = vMultisigWallets[0];
    if (walletIndex == 0)
        wallet = singleWallet;

    std::string baseKeyPath = wallet->baseKeypath();

    //try to get the xpub for seeding the request private key (ugly workaround)
    //we cannot export private keys from a hardware wallet
    executeCommandWrapper("{\"xpub\":\"" + baseKeyPath + "/1'/0\"}", DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this, walletIndex](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_XPUB_MS_REQUEST, walletIndex);
    });
}

void DBBDaemonGui::joinCopayWallet(int walletIndex)
{
    DBBWallet* wallet = vMultisigWallets[0];
    if (walletIndex == 0)
        wallet = singleWallet;

    if (walletIndex == 0) {
        DBBNetThread* thread = DBBNetThread::DetachThread();
        thread->currentThread = std::thread([this, thread, wallet]() {

            DBB::LogPrint("Creating Copay wallet...\n", "");
            //single wallet, create wallet first
            {
                std::unique_lock<std::recursive_mutex> lock(this->cs_walletObjects);
                wallet->client.CreateWallet(wallet->participationName);
            }
            emit joinCopayWalletDone(wallet);

            thread->completed();
        });

    } else {
        //check if already joined a MS wallet. if not, try to join.
        MultisigUpdateWallets(true);
    }
}

void DBBDaemonGui::joinCopayWalletComplete(DBBWallet *wallet)
{
    DBB::LogPrint("Copay wallet joined successfully, loading address..\n", "");
    getNewAddress();
    updateWallet(wallet);
    hideModalInfo();
    if (walletUpdateTimer && !walletUpdateTimer->isActive())
        walletUpdateTimer->start(WALLET_POLL_TIME);
}

void DBBDaemonGui::hidePaymentProposalsWidget()
{
    if (currentPaymentProposalWidget) {
        currentPaymentProposalWidget->hide();
        delete currentPaymentProposalWidget;
        currentPaymentProposalWidget = NULL;
    }
}

void DBBDaemonGui::updateWallet(DBBWallet* wallet)
{
    if (wallet == singleWallet) {
        SingleWalletUpdateWallets();
    } else
        MultisigUpdateWallets();
}

void DBBDaemonGui::updateUIStateMultisigWallets(bool joined)
{
    this->ui->joinCopayWallet->setVisible(!joined);
    this->ui->checkProposals->setVisible(joined);
    this->ui->multisigWalletName->setVisible(joined);
    this->ui->multisigBalanceKey->setVisible(joined);
    this->ui->multisigBalance->setVisible(joined);
    this->ui->multisigLine->setVisible(joined);
    this->ui->proposalsLabel->setVisible(joined);
    if (!joined)
        this->ui->noProposalsAvailable->setVisible(false);
}

void DBBDaemonGui::MultisigUpdateWallets(bool initialJoin)
{
    DBBWallet* wallet = vMultisigWallets[0];
    updateUIStateMultisigWallets(wallet->client.walletJoined);
    if (!wallet->client.IsSeeded())
        return;

    multisigWalletIsUpdating = true;
    executeNetUpdateWallet(wallet, true, [wallet, initialJoin, this](bool walletsAvailable, const std::string& walletsResponse) {
        emit getWalletsResponseAvailable(wallet, walletsAvailable, walletsResponse, initialJoin);
    });
}

void DBBDaemonGui::SingleWalletUpdateWallets(bool showLoading)
{
    if (singleWallet->updatingWallet)
    {
        if (showLoading) {
            setNetLoading(true);
        }
        singleWallet->shouldUpdateWalletAgain = true;
        return;
    }
    if (!singleWallet->client.IsSeeded())
        return;

    if (this->ui->balanceLabel->text() == "?") {
        this->ui->balanceLabel->setText(tr("Loading..."));
        this->ui->singleWalletBalance->setText(tr("Loading..."));

        this->ui->currentAddress->setText(tr("Loading..."));
    }

    singleWalletIsUpdating = true;
    executeNetUpdateWallet(singleWallet, showLoading, [this](bool walletsAvailable, const std::string& walletsResponse) {
        emit getWalletsResponseAvailable(this->singleWallet, walletsAvailable, walletsResponse);
    });
}

void DBBDaemonGui::updateUIMultisigWallets(const UniValue& walletResponse)
{
    vMultisigWallets[0]->updateData(walletResponse);

    if (vMultisigWallets[0]->currentPaymentProposals.isArray()) {
        this->ui->proposalsLabel->setText(tr("Current Payment Proposals (%1)").arg(vMultisigWallets[0]->currentPaymentProposals.size()));
    }

    this->ui->noProposalsAvailable->setVisible(!vMultisigWallets[0]->currentPaymentProposals.size());

    //TODO, add a monetary amount / unit helper function
    QString balance = "-";
    if (vMultisigWallets[0]->totalBalance >= 0)
        balance = QString::fromStdString(DBB::formatMoney(vMultisigWallets[0]->totalBalance));

    this->ui->multisigBalance->setText("<strong>" + balance + "</strong>");
    //TODO, Copay encrypts the wallet name. Decrypt it and display the name.
    //      Decryption: use the first 16 bytes of sha256(shared_priv_key) as the AES-CCM key;
    //      the encrypted name is in a JSON string conforming to the SJCL library format, see:
    //      https://bitwiseshiftleft.github.io/sjcl/demo/
    //this->ui->multisigWalletName->setText("<strong>Name:</strong> " + QString::fromStdString(vMultisigWallets[0]->walletRemoteName));

    updateUIStateMultisigWallets(vMultisigWallets[0]->client.walletJoined);
}

void DBBDaemonGui::updateUISingleWallet(const UniValue& walletResponse)
{
    singleWallet->updateData(walletResponse);

    //TODO, add a monetary amount / unit helper function
    QString balance = "";
    if (singleWallet->totalBalance >= 0)
        balance = QString::fromStdString(DBB::formatMoney(singleWallet->totalBalance));

    this->ui->balanceLabel->setText(balance);
    this->ui->singleWalletBalance->setText(balance);

    UniValue addressesUV = find_value(walletResponse["balance"], "byAddress");
    if (addressesUV.isArray()) {
        for (int i = 0; i < addressesUV.size(); i++) {
            UniValue address = find_value(addressesUV[i], "address");
            if (address.get_str() == this->ui->currentAddress->text().toStdString())
                getNewAddress();
        }
    }
}

void DBBDaemonGui::historyShowTx(QModelIndex index)
{
    QString txId = ui->tableWidget->model()->data(ui->tableWidget->model()->index(index.row(),0)).toString();
    QDesktopServices::openUrl(QUrl("https://" + QString(DBB_USE_TESTNET ? "testnet." : "") + "blockexplorer.com/tx/"+txId));
}

void DBBDaemonGui::updateTransactionTable(DBBWallet *wallet, bool historyAvailable, const UniValue &history)
{
    ui->tableWidget->setModel(NULL);

    this->ui->loadinghistory->setVisible(false);
    this->ui->tableWidget->setVisible(true);

    if (!historyAvailable || !history.isArray())
        return;

    transactionTableModel = new  QStandardItemModel(history.size(), 4, this);

    transactionTableModel->setHeaderData(0, Qt::Horizontal, tr("TXID"));
    transactionTableModel->setHeaderData(1, Qt::Horizontal, tr("Amount"));
    transactionTableModel->setHeaderData(2, Qt::Horizontal, tr("Address"));
    transactionTableModel->setHeaderData(3, Qt::Horizontal, tr("Date"));

    int cnt = 0;
    for (const UniValue &obj : history.getValues())
    {
        QFont font;
        font.setPointSize(12);

        UniValue actionUV = find_value(obj, "action");
        UniValue amountUV = find_value(obj, "amount");
        if (amountUV.isNum())
        {
            QString iconName;
            if (actionUV.isStr())
                iconName = ":/icons/tx_" + QString::fromStdString(actionUV.get_str());
            QStandardItem *item = new QStandardItem(QIcon(iconName), QString::fromStdString(DBB::formatMoney(amountUV.get_int64())));
            item->setToolTip(tr("Double-click for more details"));
            item->setFont(font);
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            transactionTableModel->setItem(cnt, 1, item);
        }

        UniValue addressUV = find_value(obj["outputs"][0], "address");
        if (addressUV.isStr())
        {
            QStandardItem *item = new QStandardItem(QString::fromStdString(addressUV.get_str()));
            item->setToolTip(tr("Double-click for more details"));
            item->setFont(font);
            item->setTextAlignment(Qt::AlignCenter);
            transactionTableModel->setItem(cnt, 2, item);
        }

        UniValue timeUV = find_value(obj, "time");
        UniValue confirmsUV = find_value(obj, "confirmations");
        if (timeUV.isNum())
        {
            QString iconName;
            QString tooltip;
            if (confirmsUV.isNum())
            {
                tooltip = QString::number(confirmsUV.get_int());
                if (confirmsUV.get_int() > 5)
                    iconName = ":/icons/confirm6";
                else
                    iconName = ":/icons/confirm" + QString::number(confirmsUV.get_int());
            } else {
                tooltip = "0";
                iconName = ":/icons/confirm0";
            }

            QDateTime timestamp;
            timestamp.setTime_t(timeUV.get_int64());
            QStandardItem *item = new QStandardItem(QIcon(iconName), timestamp.toString(Qt::SystemLocaleShortDate));
            item->setToolTip(tooltip + tr(" confirmations"));
            item->setTextAlignment(Qt::AlignCenter);
            item->setFont(font);
            transactionTableModel->setItem(cnt, 3, item);
        }

        UniValue txidUV = find_value(obj, "txid");
        if (txidUV.isStr())
        {
            QStandardItem *item = new QStandardItem(QString::fromStdString(txidUV.get_str()) );
            transactionTableModel->setItem(cnt, 0, item);
        }

        cnt++;
    }

    ui->tableWidget->setModel(transactionTableModel);
    ui->tableWidget->setColumnHidden(0, true);

    if (cnt) {
        ui->tableWidget->horizontalHeader()->setStretchLastSection(false);
        ui->tableWidget->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        ui->tableWidget->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        ui->tableWidget->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    } else {
        ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    }
}

void DBBDaemonGui::executeNetUpdateWallet(DBBWallet* wallet, bool showLoading, std::function<void(bool, std::string&)> cmdFinished)
{
    DBB::LogPrint("Updating copay wallet\n", "");
    DBBNetThread* thread = DBBNetThread::DetachThread();
    thread->currentThread = std::thread([this, thread, wallet, cmdFinished]() {
        std::string walletsResponse;
        std::string feeLevelResponse;

        bool walletsAvailable = false;
        {
            wallet->updatingWallet = true;

            do
            {
                {
                    std::unique_lock<std::recursive_mutex> lock(this->cs_walletObjects);
                    wallet->shouldUpdateWalletAgain = false;
                    walletsAvailable = wallet->client.GetWallets(walletsResponse);
                }
                emit getWalletsResponseAvailable(wallet, walletsAvailable, walletsResponse, false);
                bool isSingleWallet = false;
                {
                    std::unique_lock<std::recursive_mutex> lock(this->cs_walletObjects);
                    wallet->client.GetFeeLevels();
                    isSingleWallet = (wallet == this->singleWallet);
                }
                std::string txHistoryResponse;
                if (isSingleWallet) {
                    bool transactionHistoryAvailable  = false;
                    {
                        std::unique_lock<std::recursive_mutex> lock(this->cs_walletObjects);
                        transactionHistoryAvailable = wallet->client.GetTransactionHistory(txHistoryResponse);
                    }



                    UniValue data;
                    if (transactionHistoryAvailable)
                        data.read(txHistoryResponse);

                    emit getTransactionHistoryAvailable(wallet, transactionHistoryAvailable, data);
                }

            }while(wallet->shouldUpdateWalletAgain);

            wallet->updatingWallet = false;
        }
        cmdFinished(walletsAvailable, walletsResponse);
        thread->completed();
    });

    if (showLoading)
        setNetLoading(true);
}

void DBBDaemonGui::parseWalletsResponse(DBBWallet* wallet, bool walletsAvailable, const std::string& walletsResponse, bool initialJoin)
{
    setNetLoading(false);

    if (wallet == singleWallet)
        singleWalletIsUpdating = false;
    else
        multisigWalletIsUpdating = false;

    UniValue response;
    if (response.read(walletsResponse) && response.isObject()) {
        DBB::LogPrint("Got update wallet response...\n", "");

        netLoaded = true;
        if (wallet == singleWallet)
            updateUISingleWallet(response);
        else {
            updateUIMultisigWallets(response);
            MultisigUpdatePaymentProposals(response);
            if (initialJoin && !wallet->client.walletJoined)
                joinMultisigWalletInitiate(wallet);
        }
    }
    else if (walletsResponse.size() > 5) {
        if (!response.isObject())
        {
            int maxlen = 20;
            if (walletsResponse.size() < 20) {
                maxlen = walletsResponse.size();
            }
            DBB::LogPrint("Got invalid response, maybe a invalid proxy response (%s)\n", DBB::SanitizeString(walletsResponse.substr(0, maxlen)).c_str());
            emit shouldShowAlert("Error", tr("Invalid response. Are you connected to the internet? Please check your proxy settings."));
        }
    }
    else {
        if (!netLoaded) {
            netErrCount++;
            if (netErrCount > 2) {
                DBB::LogPrint("Got no response or timeout, are you connected to the internet or using an invalid proxy?\n");
                emit shouldShowAlert("Error", tr("No response or timeout. Are you connected to the internet?"));
                netErrCount = 0;
            }
        }
    }
}

bool DBBDaemonGui::MultisigUpdatePaymentProposals(const UniValue& response)
{
    bool ret = false;
    int copayerIndex = INT_MAX;

    UniValue pendingTxps;
    pendingTxps = find_value(response, "pendingTxps");
    if (!pendingTxps.isNull() && pendingTxps.isArray()) {
        std::unique_lock<std::recursive_mutex> lock(this->cs_walletObjects);
        vMultisigWallets[0]->currentPaymentProposals = pendingTxps;

        std::vector<UniValue> values = pendingTxps.getValues();
        DBB::LogPrint("Got pending multisig txps (%d)\n", values.size());
        if (values.size() == 0) {
            hidePaymentProposalsWidget();
            return false;
        }

        size_t cnt = 0;

        for (const UniValue& oneProposal : values) {
            QString amount;
            QString toAddress;

            UniValue toAddressUni = find_value(oneProposal, "toAddress");
            UniValue amountUni = find_value(oneProposal, "amount");
            UniValue actions = find_value(oneProposal, "actions");

            bool skipProposal = false;
            if (actions.isArray()) {
                for (const UniValue& oneAction : actions.getValues()) {
                    UniValue copayerId = find_value(oneAction, "copayerId");
                    UniValue actionType = find_value(oneAction, "type");

                    if (!copayerId.isStr() || !actionType.isStr())
                        continue;

                    if (vMultisigWallets[0]->client.GetCopayerId() == copayerId.get_str() && actionType.get_str() == "accept") {
                        skipProposal = true;
                        break;
                    }
                }
            }

            UniValue isUni = find_value(oneProposal, "id");
            if (isUni.isStr())
                MultisigShowPaymentProposal(pendingTxps, isUni.get_str());

            return true;
        } //end proposal loop
    }

    return ret;
}

bool DBBDaemonGui::MultisigShowPaymentProposal(const UniValue& pendingTxps, const std::string& targetID)
{
    if (pendingTxps.isArray()) {
        std::vector<UniValue> values = pendingTxps.getValues();
        if (values.size() == 0) {
            hidePaymentProposalsWidget();
            return false;
        }

        size_t cnt = 0;
        for (const UniValue& oneProposal : values) {
            UniValue idUni = find_value(oneProposal, "id");
            if (!idUni.isStr() || idUni.get_str() != targetID) {
                cnt++;
                continue;
            }

            std::string prevProposalID;
            std::string nextProposalID;

            if (cnt > 0) {
                UniValue idUni = find_value(values[cnt - 1], "id");
                if (idUni.isStr())
                    prevProposalID = idUni.get_str();
            }

            if (cnt < values.size() - 1) {
                UniValue idUni = find_value(values[cnt + 1], "id");
                if (idUni.isStr())
                    nextProposalID = idUni.get_str();
            }

            if (!currentPaymentProposalWidget) {
                currentPaymentProposalWidget = new PaymentProposal(this->ui->copay);
                connect(currentPaymentProposalWidget, SIGNAL(processProposal(DBBWallet*, const QString&, const UniValue&, int)), this, SLOT(PaymentProposalAction(DBBWallet*, const QString&, const UniValue&, int)));
                connect(currentPaymentProposalWidget, SIGNAL(shouldDisplayProposal(const UniValue&, const std::string&)), this, SLOT(MultisigShowPaymentProposal(const UniValue&, const std::string&)));
            }

            currentPaymentProposalWidget->move(15, 115);
            currentPaymentProposalWidget->show();
            currentPaymentProposalWidget->SetData(vMultisigWallets[0], vMultisigWallets[0]->client.GetCopayerId(), pendingTxps, oneProposal, prevProposalID, nextProposalID);

            cnt++;
        }
    }
    return true;
}

void DBBDaemonGui::PaymentProposalAction(DBBWallet* wallet, const QString &tfaCode, const UniValue& paymentProposal, int actionType)
{
    if (!paymentProposal.isObject())
        return;

    std::unique_lock<std::recursive_mutex> lock(this->cs_walletObjects);

    if (actionType == ProposalActionTypeReject) {
        wallet->client.RejectTxProposal(paymentProposal);
        MultisigUpdateWallets();
        return;
    } else if (actionType == ProposalActionTypeDelete) {
        wallet->client.DeleteTxProposal(paymentProposal);
        MultisigUpdateWallets();
        return;
    }
    std::vector<std::pair<std::string, std::vector<unsigned char> > > inputHashesAndPaths;
    std::string serTx;
    UniValue changeAddressData;
    wallet->client.ParseTxProposal(paymentProposal, changeAddressData, serTx, inputHashesAndPaths);

    // strip out already signed hashes
    // make a mutable copy of the sighash/keypath vector
    auto inputHashesAndPathsCopy = inputHashesAndPaths;
    auto it = inputHashesAndPathsCopy.begin();
    int amountOfCalls =  ceil((double)inputHashesAndPaths.size()/(double)MAX_INPUTS_PER_SIGN);
    while(it != inputHashesAndPathsCopy.end()) {
        std::string hexHash = DBB::HexStr((unsigned char*)&it->second[0], (unsigned char*)&it->second[0] + 32);
        if(wallet->mapHashSig.count(hexHash)) {
            // we already have this hash, remove it from the mutable copy
            it = inputHashesAndPathsCopy.erase(it);
        }
        else ++it;
    }

    // make sure the vector does not exceede the max hashes per sign commands
    if (inputHashesAndPathsCopy.size() > MAX_INPUTS_PER_SIGN)
        inputHashesAndPathsCopy.resize(MAX_INPUTS_PER_SIGN);

    //build sign command
    std::string hashCmd;
    for (const std::pair<std::string, std::vector<unsigned char> >& hashAndPathPair : inputHashesAndPathsCopy) {
        std::string hexHash = DBB::HexStr((unsigned char*)&hashAndPathPair.second[0], (unsigned char*)&hashAndPathPair.second[0] + 32);

        hashCmd += "{ \"hash\" : \"" + hexHash + "\", \"keypath\" : \"" + wallet->baseKeypath() + "/" + hashAndPathPair.first + "\" }, ";
    }
    hashCmd.pop_back();
    hashCmd.pop_back(); // remove ", "

    //build checkpubkeys
    UniValue checkpubObj = UniValue(UniValue::VARR);
    if (changeAddressData.isObject())
    {
        UniValue keypath = find_value(changeAddressData, "path");
        UniValue publicKeys = find_value(changeAddressData, "publicKeys");
        if (publicKeys.isArray())
        {
            for (const UniValue& pkey : publicKeys.getValues())
            {
                if (pkey.isStr())
                {
                    UniValue obj = UniValue(UniValue::VOBJ);
                    obj.pushKV("pubkey", pkey.get_str());
                    if (keypath.isStr())
                        obj.pushKV("keypath", wallet->baseKeypath() + "/" + keypath.get_str().substr(2));
                    checkpubObj.push_back(obj);
                }
            }
        }
    }

    std::string twoFaPart = "";
    if (!tfaCode.isEmpty())
        twoFaPart = "\"pin\" : \""+tfaCode.toStdString()+"\", ";

    uint8_t serTxHash[32];
    btc_hash((const uint8_t*)&serTx[0], serTx.size(), serTxHash);
    std::string serTxHashHex = DBB::HexStr(serTxHash, serTxHash+32);

    std::string command = "{\"sign\": { "+twoFaPart+"\"type\": \"meta\", \"meta\" : \""+serTxHashHex+"\", \"data\" : [ " + hashCmd + " ], \"checkpub\" : "+checkpubObj.write()+" } }";

    bool ret = false;
    DBB::LogPrint("Request signing...\n", "");
    executeCommandWrapper(command, DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [wallet, &ret, actionType, paymentProposal, inputHashesAndPaths, inputHashesAndPathsCopy, serTx, tfaCode, this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        //send a signal to the main thread
        processCommand = false;
        setLoading(false);

        UniValue jsonOut(UniValue::VOBJ);
        jsonOut.read(cmdOut);

        UniValue echoStr = find_value(jsonOut, "echo");
        if (!echoStr.isNull() && echoStr.isStr()) {
            UniValue jsonOutMutable = jsonOut;
            jsonOutMutable.pushKV("tx", serTx);
            emit shouldVerifySigning(wallet, paymentProposal, actionType, jsonOutMutable.write());
        } else {
            UniValue errorObj = find_value(jsonOut, "error");
            if (errorObj.isObject()) {
                //error found
                UniValue errorMessageObj = find_value(errorObj, "message");
                if (errorMessageObj.isStr())
                {
                    wallet->mapHashSig.clear();
                    DBB::LogPrint("Error while signing (%s)\n", errorMessageObj.get_str().c_str());
                    emit shouldShowAlert("Error", QString::fromStdString(errorMessageObj.get_str()));
                }

                emit shouldHideModalInfo();
                emit shouldHideVerificationInfo();
            }
            else
            {
                UniValue signObject = find_value(jsonOut, "sign");
                if (signObject.isArray()) {
                    std::vector<UniValue> vSignatureObjects;
                    vSignatureObjects = signObject.getValues();
                    if (vSignatureObjects.size() > 0) {
                        std::vector<std::string> sigs;

                        int sigCount = 0;
                        for (const UniValue& oneSig : vSignatureObjects) {
                            UniValue sigObject = find_value(oneSig, "sig");
                            if (sigObject.isNull() || !sigObject.isStr()) {
                                wallet->mapHashSig.clear();
                                DBB::LogPrint("Invalid signature from device\n", "");
                                emit shouldShowAlert("Error", tr("Invalid signature from device"));
                                return;
                            }

                            int pos = 0;
                            std::string hexHashOfSig = DBB::HexStr((unsigned char*)&inputHashesAndPathsCopy[sigCount].second[0], (unsigned char*)&inputHashesAndPathsCopy[sigCount].second[0] + 32);
                            for (const std::pair<std::string, std::vector<unsigned char> >& hashAndPathPair : inputHashesAndPaths) {
                                std::string hexHash = DBB::HexStr((unsigned char*)&hashAndPathPair.second[0], (unsigned char*)&hashAndPathPair.second[0] + 32);
                                if (hexHashOfSig == hexHash) {
                                    break;
                                }
                                pos++;
                            }
                            wallet->mapHashSig[hexHashOfSig] = std::make_pair(pos, sigObject.get_str());

                            sigCount++;
                        }

                        if (wallet->mapHashSig.size() < inputHashesAndPaths.size())
                        {
                            // we don't have all inputs signatures
                            // need another signing round:
                            emit createTxProposalDone(wallet, tfaCode, paymentProposal);
                        }
                        else {
                            // create the exact order signature array
                            std::vector<std::string> sigs;
                            int pos = 0;
                            for (const std::pair<std::string, std::vector<unsigned char> >& hashAndPathPair : inputHashesAndPaths) {
                                std::string hexHash = DBB::HexStr((unsigned char*)&hashAndPathPair.second[0], (unsigned char*)&hashAndPathPair.second[0] + 32);
                                if (wallet->mapHashSig[hexHash].first != pos) {
                                    wallet->mapHashSig.clear();
                                    DBB::LogPrint("Invalid position of inputs/signatures\n", "");
                                    emit shouldShowAlert("Error", tr("Invalid position of inputs/signatures"));
                                    return;
                                }
                                sigs.push_back(wallet->mapHashSig[hexHash].second);
                                pos++;
                            }

                            emit shouldHideVerificationInfo();
                            emit signedProposalAvailable(wallet, paymentProposal, sigs);
                            wallet->mapHashSig.clear();
                            ret = true;
                        }
                    }
                }
            }
        }
    });
}

void DBBDaemonGui::postSignaturesForPaymentProposal(DBBWallet* wallet, const UniValue& proposal, const std::vector<std::string>& vSigs)
{
    DBBNetThread* thread = DBBNetThread::DetachThread();
    thread->currentThread = std::thread([this, thread, wallet, proposal, vSigs]() {
        //thread->currentThread = ;
        if (!wallet->client.PostSignaturesForTxProposal(proposal, vSigs))
        {
            DBB::LogPrint("Error posting txp signatures\n", "");
            emit shouldHideModalInfo();
            emit shouldHideVerificationInfo();
            emit shouldShowAlert("Error", tr("Could not post signatures"));
        }
        else
        {
            if (!wallet->client.BroadcastProposal(proposal))
            {
                DBB::LogPrint("Error broadcasting transaction\n", "");

                //hack: sleep and try again
                std::this_thread::sleep_for(std::chrono::milliseconds(3000));
                if (!wallet->client.BroadcastProposal(proposal))
                {
                    emit shouldHideModalInfo();
                    DBB::LogPrint("Error broadcasting transaction\n", "");
                    emit shouldShowAlert("Error", tr("Could not broadcast transaction"));
                }
            }
            else
            {
                //sleep 3 seconds to get time for the wallet server to process the transaction and response with the correct balance
                std::this_thread::sleep_for(std::chrono::milliseconds(3000));

                emit shouldUpdateWallet(wallet);
                emit paymentProposalUpdated(wallet, proposal);
            }
        }

        thread->completed();
    });
    DBB::LogPrint("Broadcast Transaction\n", "");
    showModalInfo(tr("Broadcast Transaction"));
    setNetLoading(true);
}

#pragma mark - Smart Verification Stack (ECDH / ComServer)

void DBBDaemonGui::sendECDHPairingRequest(const std::string &ecdhRequest)
{
    if (!deviceReadyToInteract)
        return;

    DBB::LogPrint("Paring request\n", "");

    executeCommandWrapper("{\"verifypass\": "+ecdhRequest+"}", DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_VERIFYPASS_ECDH);
    });
    showModalInfo("Pairing Verification Device");
}

void DBBDaemonGui::comServerMessageParse(const QString& msg)
{
    // pass in a push message from the communication server
    // will be called on main thread

    // FIXME: only send a ECDH request if the messages is a ECDH p-req.
    UniValue json;
    json.read(msg.toStdString());

    //check the type of the notification
    UniValue possiblePINObject = find_value(json, "pin");
    UniValue possibleECDHObject = find_value(json, "ecdh");
    UniValue possibleIDObject = find_value(json, "id");
    UniValue possibleRandomObject = find_value(json, "random");
    UniValue possibleActionObject = find_value(json, "action");
    if (possiblePINObject.isStr())
    {
        //feed the modal view with the 2FA code
        QString pinCode = QString::fromStdString(possiblePINObject.get_str());
        if (pinCode == "abort")
            pinCode.clear();

        ui->modalBlockerView->proceedFrom2FAToSigning(pinCode);
    }
    else if (possibleECDHObject.isStr())
    {
        sendECDHPairingRequest(msg.toStdString());
    }
    else if (possibleIDObject.isStr())
    {
        if (possibleIDObject.get_str() == "success")
            hideModalInfo();
    }
    else if (possibleRandomObject.isStr())
    {
        if (possibleRandomObject.get_str() == "clear")
            hideModalInfo();
    }
    else if (possibleActionObject.isStr() && possibleActionObject.get_str() == "pong")
    {
        lastPing = 0;
        this->statusBarVDeviceIcon->setToolTip(tr("Verification device connected"));
        this->statusBarVDeviceIcon->setVisible(true);
        comServer->mobileAppConnected = true;
    }
}

void DBBDaemonGui::pairSmartphone()
{
    //create a new channel id and encryption key
    bool generateData = true;
    if (!comServer->getChannelID().empty())
    {
        QMessageBox::StandardButton reply = QMessageBox::question(this, "", tr("Would you like to re-pair your device (create a new key)?"), QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::No) {
            generateData = false;
        }
    }

    if (generateData) {
        comServer->generateNewKey();
        configData->setComServerChannelID(comServer->getChannelID());
        configData->setComServerEncryptionKey(comServer->getEncryptionKey());
        configData->write();
        comServer->setChannelID(configData->getComServerChannelID());
        comServer->startLongPollThread();
        pingComServer();
    }

    QString pairingData = QString::fromStdString(comServer->getPairData());
    showModalInfo(tr("Scan the QR code using the Digital Bitbox mobile app.")+"<br/><br /><font color=\"#999999\" style=\"font-size: small\">Connection-Code:<br />"+QString::fromStdString(comServer->getChannelID())+":"+QString::fromStdString(comServer->getAESKeyBase58())+"</font>", DBB_PROCESS_INFOLAYER_CONFIRM_WITH_BUTTON);
    updateModalWithQRCode(pairingData);
}

void DBBDaemonGui::showSettings()
{
    if (!settingsDialog)
    {
        settingsDialog = new SettingsDialog(this, configData, cachedDeviceLock);
        connect(settingsDialog, SIGNAL(settingsDidChange()), this, SLOT(updateSettings()));
        connect(settingsDialog, SIGNAL(settingsShouldChangeHiddenPassword(const QString&)), this, SLOT(updateHiddenPassword(const QString&)));
        connect(settingsDialog, SIGNAL(settingsShouldResetU2F()), this, SLOT(resetU2F()));
    }

    settingsDialog->updateDeviceLocked(cachedDeviceLock);
    settingsDialog->show();
}

void DBBDaemonGui::updateSettings()
{
    vMultisigWallets[0]->setBackendURL(configData->getBWSBackendURL());
    vMultisigWallets[0]->setSocks5ProxyURL(configData->getSocks5ProxyURL());
    singleWallet->setBackendURL(configData->getBWSBackendURL());
    singleWallet->setSocks5ProxyURL(configData->getSocks5ProxyURL());

    if (comServer)
    {
        comServer->setURL(configData->getComServerURL());
        comServer->setSocks5ProxyURL(configData->getSocks5ProxyURL());
    }

    if (updateManager)
        updateManager->setSocks5ProxyURL(configData->getSocks5ProxyURL());
}

void DBBDaemonGui::updateHiddenPassword(const QString& hiddenPassword)
{
    DBB::LogPrint("Set hidden password\n", "");

    executeCommandWrapper("{\"hidden_password\": \""+hiddenPassword.toStdString()+"\"}", DBB_PROCESS_INFOLAYER_STYLE_TOUCHBUTTON, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_RESET_PASSWORD);
    });
}

void DBBDaemonGui::showQrCodeScanner()
{
#ifdef DBB_USE_MULTIMEDIA
    if (!qrCodeScanner)
    {
        qrCodeScanner = new DBBQRCodeScanner(this);
        connect(qrCodeScanner, SIGNAL(QRCodeFound(const QString&)), this, SLOT(qrCodeFound(const QString&)));
    }

    qrCodeScanner->show();
    qrCodeScanner->setScannerActive(true);
#endif
}

void DBBDaemonGui::qrCodeFound(const QString &payload)
{
#ifdef DBB_USE_MULTIMEDIA
    static const char bitcoinurl[] = "bitcoin:";
    static const char amountfield[] = "amount=";

    bool validQRCode = false;
    if (payload.startsWith(bitcoinurl, Qt::CaseInsensitive))
    {
        // get the part after the "bitcoin:"
        QString addressWithDetails = payload.mid(strlen(bitcoinurl));

        // form a substring with only the address
        QString onlyAddress = addressWithDetails.mid(0,addressWithDetails.indexOf("?"));

        // if there is an amount, rip our the string
        if (addressWithDetails.indexOf(amountfield) != -1)
        {
            QString part = addressWithDetails.mid(addressWithDetails.indexOf(amountfield));
            QString amount = part.mid(strlen(amountfield),part.indexOf("&")-strlen(amountfield));

            // fill amount
            this->ui->sendAmount->setText(amount);
        }

        // fill address
        this->ui->sendToAddress->setText(onlyAddress);
        validQRCode = true;
    }

    qrCodeScanner->setScannerActive(false);
    qrCodeScanner->hide();

    if (!validQRCode)
        showAlert(tr("Invalid Bitcoin QRCode"), tr("The scanned QRCode does not contain a valid Bitcoin address."));
#endif
}

inline bool file_exists (const char *name) {
    struct stat buffer;
    int result = stat(name, &buffer);
    return (result == 0);
}

void DBBDaemonGui::checkUDevRule()
{
#if DBB_ENABLE_UDEV_CHECK
    static const int WARNING_NEVER_SHOW_AGAIN = 2;
    const char *udev_rules_file = "/etc/udev/rules.d/52-hid-digitalbitbox.rules";
    QSettings settings;
    if (settings.value("udev_warning_state", 0).toInt() != WARNING_NEVER_SHOW_AGAIN && !file_exists(udev_rules_file))
    {
        QMessageBox msgBox;
        msgBox.setText(tr("Linux udev rule"));
        msgBox.setInformativeText(tr("It looks like you are running on Linux and don't have the required udev rule."));
        QAbstractButton *dontWarnAgainButton = msgBox.addButton(tr("Don't warn me again"), QMessageBox::RejectRole);
        QAbstractButton *showHelpButton = msgBox.addButton(tr("Show online manual"), QMessageBox::HelpRole);
        msgBox.addButton(QMessageBox::Ok);
        msgBox.exec();
        if (msgBox.clickedButton() == dontWarnAgainButton)
        {
            settings.setValue("udev_warning_state", WARNING_NEVER_SHOW_AGAIN);
        }
       if (msgBox.clickedButton() == showHelpButton)
        {
            QDesktopServices::openUrl(QUrl("https://digitalbitbox.com/start_linux#udev?app=dbb-app"));
        }
    }
#endif
}
