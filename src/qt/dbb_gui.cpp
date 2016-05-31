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
#include <QMessageBox>
#include <QNetworkInterface>
#include <QSpacerItem>
#include <QTimer>
#include <QToolBar>
#include <QFontDatabase>
#include <QGraphicsOpacityEffect>
#include <QtNetwork/QHostInfo>

#include "ui/ui_overview.h"
#include <dbb.h>

#include "dbb_util.h"
#include "dbb_netthread.h"
#include "bonjourserviceregister.h"

#include <cstdio>
#include <ctime>
#include <chrono>
#include <fstream>

#include <univalue.h>
#include <btc/bip32.h>
#include <btc/tx.h>

#include <qrencode.h>

#if defined _MSC_VER
#include <direct.h>
#elif defined __GNUC__
#include <sys/types.h>
#include <sys/stat.h>
#endif

static std::string ca_file;

//function from dbb_app.cpp
extern void executeCommand(const std::string& cmd, const std::string& password, std::function<void(const std::string&, dbb_cmd_execution_status_t status)> cmdFinished);
extern void setFirmwareUpdateHID(bool state);

DBBDaemonGui::~DBBDaemonGui()
{
    delete bonjourRegister;
}

void testfunc(DNSServiceRef, DNSServiceFlags,
              DNSServiceErrorType errorCode, const char *name,
              const char *regtype, const char *domain,
              void *data)
{

}

DBBDaemonGui::DBBDaemonGui(const QString& uri, QWidget* parent) : QMainWindow(parent),
                                              openedWithBitcoinURI(0),
                                              ui(new Ui::MainWindow),
                                              statusBarButton(0),
                                              statusBarLabelRight(0),
                                              statusBarLabelLeft(0),
                                              backupDialog(0),
                                              getAddressDialog(0),
                                              verificationDialog(0),
                                              websocketServer(0),
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
                                              checkingForUpdates(0)
{
#if defined(Q_OS_MAC)
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    ui->setupUi(this);

#if defined(__linux__) || defined(__unix__)
    //need to libcurl, load it once, set the CA path at runtime
    ca_file = getCAFile();
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
    std::string stdFontSize = "16pt";
    std::string smallFontSize = "12pt";
#elif defined(Q_OS_WIN)
    std::string balanceFontSize = "24pt";
    std::string menuFontSize = "18pt";
    std::string stdFontSize = "16pt";
    std::string smallFontSize = "12pt";
#else
    std::string balanceFontSize = "20pt";
    std::string menuFontSize = "14pt";
    std::string stdFontSize = "12pt";
    std::string smallFontSize = "10pt";
#endif

    QFontDatabase::addApplicationFont(":/fonts/BebasKai-Regular");
    QFontDatabase::addApplicationFont(":/fonts/SourceSansPro-ExtraLight");
    QFontDatabase::addApplicationFont(":/fonts/SourceSansPro-Bold");
    QFontDatabase::addApplicationFont(":/fonts/SourceSansPro-Black");
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

    this->ui->betaIcon->setAttribute(Qt::WA_TransparentForMouseEvents);

    this->ui->balanceLabel->setStyleSheet("font-size: " + QString::fromStdString(balanceFontSize) + ";");
    this->ui->singleWalletBalance->setStyleSheet("font-size: " + QString::fromStdString(balanceFontSize) + ";");
    this->ui->multisigBalance->setStyleSheet("font-size: " + QString::fromStdString(balanceFontSize) + ";");
    ////////////// END STYLING

    // allow serval signaling data types
    qRegisterMetaType<UniValue>("UniValue");
    qRegisterMetaType<std::string>("std::string");
    qRegisterMetaType<dbb_cmd_execution_status_t>("dbb_cmd_execution_status_t");
    qRegisterMetaType<dbb_response_type_t>("dbb_response_type_t");
    qRegisterMetaType<std::vector<std::string> >("std::vector<std::string>");
    qRegisterMetaType<DBBWallet*>("DBBWallet *");

    // connect UI
    connect(ui->eraseButton, SIGNAL(clicked()), this, SLOT(eraseClicked()));
    connect(ui->ledButton, SIGNAL(clicked()), this, SLOT(ledClicked()));
    connect(ui->passwordButton, SIGNAL(clicked()), this, SLOT(showSetPasswordInfo()));
    connect(ui->seedButton, SIGNAL(clicked()), this, SLOT(seedHardware()));
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
    connect(ui->upgradeFirmware, SIGNAL(clicked()), this, SLOT(upgradeFirmware()));
    ui->upgradeFirmware->setVisible(false);
    ui->keypathLabel->setVisible(false);//hide keypath label for now (only tooptip)
    connect(ui->checkForUpdates, SIGNAL(clicked()), this, SLOT(checkForUpdate()));
    connect(ui->tableWidget, SIGNAL(doubleClicked(QModelIndex)),this,SLOT(historyShowTx(QModelIndex)));
    connect(ui->deviceNameLabel, SIGNAL(clicked()),this,SLOT(setDeviceNameClicked()));

    // connect custom signals
    connect(this, SIGNAL(XPubForCopayWalletIsAvailable(int)), this, SLOT(getRequestXPubKeyForCopay(int)));
    connect(this, SIGNAL(RequestXPubKeyForCopayWalletIsAvailable(int)), this, SLOT(joinCopayWallet(int)));
    connect(this, SIGNAL(gotResponse(const UniValue&, dbb_cmd_execution_status_t, dbb_response_type_t, int)), this, SLOT(parseResponse(const UniValue&, dbb_cmd_execution_status_t, dbb_response_type_t, int)));
    connect(this, SIGNAL(shouldVerifySigning(DBBWallet*, const UniValue&, int, const std::string&)), this, SLOT(showEchoVerification(DBBWallet*, const UniValue&, int, const std::string&)));
    connect(this, SIGNAL(shouldHideVerificationInfo()), this, SLOT(hideVerificationInfo()));
    connect(this, SIGNAL(signedProposalAvailable(DBBWallet*, const UniValue&, const std::vector<std::string>&)), this, SLOT(postSignaturesForPaymentProposal(DBBWallet*, const UniValue&, const std::vector<std::string>&)));
    connect(this, SIGNAL(getWalletsResponseAvailable(DBBWallet*, bool, const std::string&)), this, SLOT(parseWalletsResponse(DBBWallet*, bool, const std::string&)));
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

    connect(this, SIGNAL(checkForUpdateResponseAvailable(const std::string&, long, bool)), this, SLOT(parseCheckUpdateResponse(const std::string&, long, bool)));



    // create backup dialog instance
    backupDialog = new BackupDialog(0);
    connect(backupDialog, SIGNAL(addBackup()), this, SLOT(addBackup()));
    connect(backupDialog, SIGNAL(eraseAllBackups()), this, SLOT(eraseAllBackups()));
    connect(backupDialog, SIGNAL(eraseBackup(const QString&)), this, SLOT(eraseBackup(const QString&)));
    connect(backupDialog, SIGNAL(restoreFromBackup(const QString&)), this, SLOT(restoreBackup(const QString&)));

    // create get address dialog
    getAddressDialog = new GetAddressDialog(0);
    connect(getAddressDialog, SIGNAL(shouldGetXPub(const QString&)), this, SLOT(getAddressGetXPub(const QString&)));

    //set window icon
    QApplication::setWindowIcon(QIcon(":/icons/dbb"));
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
    this->statusBarLabelLeft = new QLabel(tr("No Device Found"));
    statusBar()->addWidget(this->statusBarLabelLeft);

    this->statusBarLabelRight = new QLabel("");
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
    }


    connect(this->ui->overviewButton, SIGNAL(clicked()), this, SLOT(mainOverviewButtonClicked()));
    connect(this->ui->multisigButton, SIGNAL(clicked()), this, SLOT(mainMultisigButtonClicked()));
    connect(this->ui->receiveButton, SIGNAL(clicked()), this, SLOT(mainReceiveButtonClicked()));
    connect(this->ui->sendButton, SIGNAL(clicked()), this, SLOT(mainSendButtonClicked()));
    connect(this->ui->mainSettingsButton, SIGNAL(clicked()), this, SLOT(mainSettingsButtonClicked()));

    //login screen setup
    this->ui->blockerView->setVisible(false);
    connect(this->ui->passwordLineEdit, SIGNAL(returnPressed()), this, SLOT(passwordProvided()));

    //set password screen
    connect(this->ui->modalBlockerView, SIGNAL(newPasswordAvailable(const QString&, const QString&, bool)), this, SLOT(setPasswordProvided(const QString&, const QString&, bool)));
    connect(this->ui->modalBlockerView, SIGNAL(signingShouldProceed(const QString&, void *, const UniValue&, int)), this, SLOT(proceedVerification(const QString&, void *, const UniValue&, int)));
    //modal general signals
    connect(this->ui->modalBlockerView, SIGNAL(modalViewWillShowHide(bool)), this, SLOT(modalStateChanged(bool)));

    //create the single and multisig wallet
    std::string dataDir = DBB::GetDefaultDBBDataDir();
    singleWallet = new DBBWallet(dataDir, DBB_USE_TESTNET);
    singleWallet->setBaseKeypath(DBB::GetArg("-keypath", DBB_USE_TESTNET ? "m/44'/1'" : "m/44'/0'"));
    DBBWallet* copayWallet = new DBBWallet(dataDir, DBB_USE_TESTNET);
    copayWallet->setBaseKeypath(DBB::GetArg("-mskeypath","m/100'/45'/0'"));

#if defined(__linux__) || defined(__unix__)
    singleWallet->setCAFile(ca_file);
    copayWallet->setCAFile(ca_file);
#endif

    vMultisigWallets.push_back(copayWallet);


    deviceConnected = false;
    resetInfos();
    //set status bar connection status
    uiUpdateDeviceState();
    changeConnectedState(DBB::isConnectionOpen(), DBB::deviceAvailable());


    processCommand = false;

    //connect the device status update at very last point in init
    connect(this, SIGNAL(deviceStateHasChanged(bool, int)), this, SLOT(changeConnectedState(bool, int)));

    //create a local websocket server
    websocketServer = new WebsocketServer(WEBSOCKET_PORT, false);
    connect(websocketServer, SIGNAL(ecdhPairingRequest(const std::string&)), this, SLOT(sendECDHPairingRequest(const std::string&)));
    connect(websocketServer, SIGNAL(amountOfConnectionsChanged(int)), this, SLOT(amountOfPairingDevicesChanged(int)));

    //announce service over mDNS
    bonjourRegister = new BonjourServiceRegister(this);
    bonjourRegister->registerService(BonjourRecord("Digital Bitbox App Websocket", QLatin1String("_dbb._tcp."), QString()), WEBSOCKET_PORT);

    walletUpdateTimer = new QTimer(this);
    connect(walletUpdateTimer, SIGNAL(timeout()), this, SLOT(updateTimerFired()));

    QTimer::singleShot(200, this, SLOT(checkForUpdateInBackground()));
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

    // special case for firmware upgrades
    if (upgradeFirmwareState && stateChanged)
    {
        deviceConnected = state;

        if (deviceType == DBB::DBB_DEVICE_MODE_BOOTLOADER && state)
                upgradeFirmwareWithFile(firmwareFileToUse);
        return;
    }

    if (stateChanged) {
        if (state && (deviceType == DBB::DBB_DEVICE_MODE_FIRMWARE || deviceType == DBB::DBB_DEVICE_MODE_FIRMWARE_NO_PASSWORD)) {
            deviceConnected = true;
            //: translation: device connected status bar
            DBB::LogPrint("Device connected\n", "");
            this->statusBarLabelLeft->setText(tr("Device Connected"));
            this->statusBarButton->setVisible(true);
        } else {
            deviceConnected = false;
            DBB::LogPrint("Device disconnected\n", "");
            this->statusBarLabelLeft->setText(tr("No Device Found"));
            this->statusBarButton->setVisible(false);
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

    //: translation: status bar info text during the time of USB communication
    this->statusBarLabelRight->setText((status) ? tr("processing...") : "");

    this->statusBarUSBIcon->setVisible(status);

    //: translation: login screen info text during password USB check (device info)
    this->ui->unlockingInfo->setText((status) ? tr("Unlocking Device...") : "");
}

void DBBDaemonGui::setNetLoading(bool status)
{
    //: translation: status bar info text during network activity (copay)
    this->statusBarLabelRight->setText((status) ? tr("loading...") : "");
    this->statusBarNetIcon->setVisible(status);
}

void DBBDaemonGui::resetInfos()
{
    //: translation: info text for version and device name during loading getinfo
    this->ui->versionLabel->setText(tr("loading info..."));
    this->ui->deviceNameLabel->setText(tr("loading info..."));

    updateOverviewFlags(false, false, true);

    //reset wallet
    ui->tableWidget->setModel(NULL);
    ui->balanceLabel->setText("");
    ui->singleWalletBalance->setText("");
    ui->qrCode->setIcon(QIcon());
    ui->qrCode->setToolTip("");
    ui->keypathLabel->setText("");
    ui->currentAddress->setText("");
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
        if (websocketServer)
            websocketServer->abortECDHPairing();

        //clear some infos
        sdcardWarned = false;

        //remove the wallets
        singleWallet->client.setNull();
        vMultisigWallets[0]->client.setNull();

        if (walletUpdateTimer)
            walletUpdateTimer->stop();

    } else {
        if (deviceType == DBB::DBB_DEVICE_MODE_FIRMWARE)
        {
            hideModalInfo();
            askForSessionPassword();
        }
        else if (deviceType == DBB::DBB_DEVICE_MODE_FIRMWARE_NO_PASSWORD)
        {
            hideModalInfo();
            showSetPasswordInfo(true);
        }
    }
}

void DBBDaemonGui::updateTimerFired()
{
    SingleWalletUpdateWallets(false);
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

    int amountOfClientsInformed = 0;
    if (websocketServer)
    {
        amountOfClientsInformed = websocketServer->sendStringToAllClients(echoStr);
        if (amountOfClientsInformed > 0)
            verificationActivityAnimation->start(QAbstractAnimation::KeepWhenStopped);
    }

    ui->modalBlockerView->setTXVerificationData(wallet, proposalData, echoStr, actionType);
    ui->modalBlockerView->showTransactionVerification(cachedDeviceLock, (amountOfClientsInformed == 0));

    if (!cachedDeviceLock)
    {
        if (amountOfClientsInformed > 0)
        {
            //no follow up action required, clear TX data
            ui->modalBlockerView->clearTXData();

            //directly start DBB signing process
            PaymentProposalAction(wallet, "", proposalData, actionType);
        }
        else
        {
            //no verification device connected, start QRCode based verification
            QMessageBox::warning(this, tr(""), tr("A device running the Digital Bitbox mobile app is not detected on the WiFi network. Instead, you can verify the transaction by scanning the sequence of QR codes."), QMessageBox::Ok);
        }

    }
    else
        updateModalWithIconName(":/icons/twofahelp");
}

void DBBDaemonGui::proceedVerification(const QString& twoFACode, void *ptr, const UniValue& proposalData, int actionType)
{
    updateModalWithIconName(":/icons/touchhelp");

    if (ptr == NULL && twoFACode.isEmpty())
    {
        //cancle pressed
        ui->modalBlockerView->clearTXData();
        hideModalInfo();
        return;
    }
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

    sessionPassword = this->ui->passwordLineEdit->text().toStdString();
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

void DBBDaemonGui::showSetPasswordInfo(bool newWallet)
{
    ui->modalBlockerView->showSetPasswordInfo(newWallet);
}

void DBBDaemonGui::setPasswordProvided(const QString& newPassword, const QString& extraInput, bool newWallet)
{
    dbb_process_infolayer_style_t process; 
    std::string command = "{\"password\" : \"" + newPassword.toStdString() + "\"}";
    
    if (newWallet) {
        deviceName = extraInput;
        process = DBB_PROCESS_INFOLAYER_STYLE_NO_INFO;
    } else {
        if (extraInput.toStdString() != sessionPassword) {
            showModalInfo(tr("Incorrect old password"), DBB_PROCESS_INFOLAYER_CONFIRM_WITH_BUTTON);
            return;
        }
        process = DBB_PROCESS_INFOLAYER_STYLE_TOUCHBUTTON;
    }

    showModalInfo(tr("Saving Password"));

    if (executeCommandWrapper(command, process, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
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

void DBBDaemonGui::ledClicked()
{
    executeCommandWrapper("{\"led\" : \"toggle\"}", DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
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

void DBBDaemonGui::seedHardware()
{
    if (sessionPassword.empty() || sessionPassword.size() > 64)
        return;

    std::string hashHex = DBB::getStretchedBackupHexKey(sessionPassword);

    DBB::LogPrint("Request device seeding...\n", "");
    std::string command = "{\"seed\" : {"
                                "\"source\" :\"create\","
                                "\"decrypt\": \"yes\","
                                "\"key\": \""+hashHex+"\""
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
    executeCommandWrapper("{\"random\" : \"true\" }", DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_RANDOM_NUM);
    });
}

void DBBDaemonGui::lockDevice()
{
    QMessageBox::StandardButton reply = QMessageBox::question(this, "", tr("Be sure to backup your wallet and pair the mobile app before enabling two-factor authentication. After, app pairing and the micro SD card slot (wallet backup and recovery) will be disabled. They can be re-enabled only by resetting and erasing the device. Proceed?"), QMessageBox::Yes | QMessageBox::No);
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

void DBBDaemonGui::upgradeFirmware()
{
    //: translation: Open file dialog help text
    firmwareFileToUse = QFileDialog::getOpenFileName(this, tr("Select Firmware"), "", tr("DBB Firmware Files (*.bin *.dbb)"));
    if (firmwareFileToUse.isNull())
        return;

    DBB::LogPrint("Request bootloader unlock\n", "");
    executeCommandWrapper("{\"bootloader\" : \"unlock\" }", DBB_PROCESS_INFOLAYER_STYLE_TOUCHBUTTON, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_BOOTLOADER_UNLOCK);
    });
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
        showModalInfo("<strong>Upgrading Firmware...</strong><br/><br/>Please stand by...", DBB_PROCESS_INFOLAYER_STYLE_NO_INFO);

        fwUpgradeThread = new std::thread([this,possibleFilename]() {
            bool upgradeRes = false;
            // dummy private key to allow current testing
            // the private key matches the pubkey on the DBB bootloader / FW
            std::string testing_privkey = "e0178ae94827844042d91584911a6856799a52d89e9d467b83f1cf76a0482a11";

            // load the file
            std::ifstream firmwareFile(possibleFilename, std::ios::binary | std::ios::ate);
            std::streamsize firmwareSize = firmwareFile.tellg();
            if (firmwareSize > 0)
            {
                firmwareFile.seekg(0, std::ios::beg);

                std::vector<char> firmwareBuffer(DBB_APP_LENGTH);
                unsigned int pos = 0;
                while (true)
                {
                    //read into
                    firmwareFile.read(&firmwareBuffer[0]+pos, FIRMWARE_CHUNKSIZE);
                    std::streamsize bytes = firmwareFile.gcount();
                    if (bytes == 0)
                        break;

                    pos += bytes;
                }
                firmwareFile.close();

                // append 0xff to the rest of the firmware buffer
                memset((void *)(&firmwareBuffer[0]+pos), 0xff, DBB_APP_LENGTH-pos);

                // generate a double SHA256 of the firmware data
                uint8_t hashout[32];
                btc_hash((const uint8_t*)&firmwareBuffer[0], firmwareBuffer.size(), hashout);
                std::string hashHex = DBB::HexStr(hashout, hashout+32);

                // sign and get the compact signature
                btc_key key;
                btc_privkey_init(&key);
                std::vector<unsigned char> privkey = DBB::ParseHex(testing_privkey);
                memcpy(&key.privkey, &privkey[0], 32);

                size_t sizeout = 64;
                unsigned char sig[sizeout];
                int res = btc_key_sign_hash_compact(&key, hashout, sig, &sizeout);
                std::string sigStr = DBB::HexStr(sig, sig+sizeout);

                emit shouldUpdateModalInfo("<strong>Upgrading Firmware...</strong><br/><br/>Hold down the touch button for serval seconds to allow to erase your current firmware and upload the new one.");
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
        upgradeFirmwareState = false;
        setFirmwareUpdateHID(false);
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
        //: translation: successfull firmware update text
        DBB::LogPrint("Firmware successfully upgraded\n", "");
        showModalInfo(tr("Firmware upgraded successfully. Please unplug/plug your Digital Bitbox."), DBB_PROCESS_INFOLAYER_STYLE_REPLUG);
    }
    else
    {
        //: translation: firmware upgrade error
        DBB::LogPrint("Error while upgrading firmware\n", "");
        showAlert(tr("Firmware Upgrade"), tr("Error while upgrading firmware. Please unplug/plug your Digital Bitbox."));
    }


}

void DBBDaemonGui::setDeviceNameClicked()
{
    bool ok;
    deviceName = QInputDialog::getText(this, "", tr("Enter device name"), QLineEdit::Normal, "", &ok);
    if (!ok || deviceName.isEmpty())
        return;

    QRegExp nameMatcher("^[0-9A-Z-_ ]{1,64}$", Qt::CaseInsensitive);
    if (!nameMatcher.exactMatch(deviceName))
    {
        showModalInfo(tr("The device name must only contain alphanumeric characters and - or _"), DBB_PROCESS_INFOLAYER_CONFIRM_WITH_BUTTON);
        return;
    }

    setDeviceName(DBB_RESPONSE_TYPE_SET_DEVICE_NAME);
}

void DBBDaemonGui::setDeviceName(dbb_response_type_t response_type)
{
    std::string command = "{\"name\" : \""+deviceName.toStdString()+"\" }";
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
    std::time_t rawtime;
    std::tm* timeinfo;
    char buffer[80];

    std::time(&rawtime);
    timeinfo = std::localtime(&rawtime);

    std::strftime(buffer, 80, "%Y-%m-%d-%H-%M-%S", timeinfo);
    std::string timeStr(buffer);

    std::string hashHex = DBB::getStretchedBackupHexKey(sessionPassword);

    std::string command = "{\"backup\" : {"
                            "\"encrypt\" :\"yes\","
                            "\"filename\": \"backup-" + timeStr + ".bak\","
                            "\"key\": \""+hashHex+"\""
                          "} }";

    DBB::LogPrint("Adding a backup (%s)\n", timeStr.c_str());
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
    std::string command = "{\"backup\" : { \"decrypt\": \"yes\", \"check\" : \"" + backupFilename.toStdString() + "\" } }";

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
    std::string hashHex = DBB::getStretchedBackupHexKey(sessionPassword);
    std::string command = "{\"seed\" : {"
                                "\"source\" :\"" + backupFilename.toStdString() + "\","
                                "\"decrypt\": \"yes\","
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
                showSetPasswordInfo(true);
            } else {
                //password wrong
                showAlert(tr("Error"), QString::fromStdString(errorMessageObj.get_str()));
                errorShown = true;
            }
        } else if (tag == DBB_RESPONSE_TYPE_INFO) {
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
                if (cachedDeviceLock)
                    ui->lockDevice->setText(tr("Full 2FA is enabled"));
                else
                    ui->lockDevice->setText(tr("Enable Full 2FA"));

                //update version and name
                if (version.isStr())
                    this->ui->versionLabel->setText(QString::fromStdString(version.get_str()));
                if (name.isStr())
                    this->ui->deviceNameLabel->setText("<strong>Name:</strong> "+QString::fromStdString(name.get_str()));

                this->ui->DBBAppVersion->setText("DBB v"+QString(DBB_PACKAGE_VERSION) + "-" + VERSION);

                updateOverviewFlags(cachedWalletAvailableState, cachedDeviceLock, false);

                bool shouldCreateSingleWallet = false;
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
                            shouldCreateSingleWallet = true;
                    }
                    if (vMultisigWallets[0]->client.getFilenameBase().empty())
                    {
                        vMultisigWallets[0]->client.setFilenameBase(walletIDUV.get_str()+"_copay_multisig_0");
                        vMultisigWallets[0]->client.LoadLocalData();
                    }
                }
                if (shouldCreateSingleWallet)
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
                        //: translation: warning text if SDCard needs to be insert for wallet creation
                        showModalInfo(tr("Please insert a SDCard and replug the device. Initializing the wallet is only possible with a SDCard (otherwise you don't have a backup!)."));
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
                    //: translation: warning text if SDCard is insert in productive environment
                    showModalInfo(tr("Keep the SD card safe unless doing backups or restores"), DBB_PROCESS_INFOLAYER_CONFIRM_WITH_BUTTON);
                    updateModalWithIconName(":/icons/touchhelp_sdcard");

                    sdcardWarned = true;
                }

                // special case for post firmware upgrades (lock bootloader)
                if (!shouldKeepBootloaderState && bootlock.isFalse())
                {
                    // unlock bootloader
                    //: translation: modal infotext for guiding user to lock the bootloader
                    showModalInfo("<strong>Lock Firmware...</strong><br/><br/>You need to lock your Digital Bitbox to protect from further unintentional firmware upgrades.", DBB_PROCESS_INFOLAYER_STYLE_TOUCHBUTTON);
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
            bool sentToWebsocketClients = false;

            UniValue responseMutable(UniValue::VOBJ);
            UniValue requestXPubKeyUV = find_value(response, "xpub");
            UniValue requestXPubEchoUV = find_value(response, "echo");
            QString errorString;
            if (requestXPubKeyUV.isStr() && requestXPubEchoUV.isStr()) {
                //pass the response to the verification devices
                responseMutable.pushKV("echo", requestXPubEchoUV.get_str().c_str());
                if (subtag == DBB_ADDRESS_STYLE_MULTISIG_1OF1)
                    responseMutable.pushKV("type", "p2sh_ms_1of1");
                if (subtag == DBB_ADDRESS_STYLE_P2PKH)
                    responseMutable.pushKV("type", "p2pkh");
                if (websocketServer->sendStringToAllClients(responseMutable.write()) > 0)
                {
                    sentToWebsocketClients = true;
                    verificationActivityAnimation->start(QAbstractAnimation::KeepWhenStopped);
                }



            }
            if (!sentToWebsocketClients)
            {
                if (!verificationDialog)
                    verificationDialog = new VerificationDialog();

                verificationDialog->show();
                verificationDialog->setData(tr("Securely Verify Your Receiving Address"), tr("A device running the Digital Bitbox mobile app is not detected on the WiFi network. Instead, you can verify the address by scanning QR codes."), responseMutable.write());
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
            UniValue randomNumObj = find_value(response, "random");
            if (randomNumObj.isStr()) {
                showModalInfo("<strong>"+tr("Random hexadecimal number")+"</strong><br /><br />"+QString::fromStdString(randomNumObj.get_str()+""), DBB_PROCESS_INFOLAYER_CONFIRM_WITH_BUTTON);
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
            setFirmwareUpdateHID(true);
            showModalInfo("<strong>Upgrading Firmware...</strong><br/><br/>Please unplung/plug your Digital Bitbox. Briefly hold the touchbutton to allow upgrading to a new Firmware.", DBB_PROCESS_INFOLAYER_STYLE_REPLUG);
        }
        else if (tag == DBB_RESPONSE_TYPE_BOOTLOADER_LOCK) {
            hideModalInfo();
        }
        else if (tag == DBB_RESPONSE_TYPE_SET_DEVICE_NAME || tag == DBB_RESPONSE_TYPE_SET_DEVICE_NAME_CREATE) {
            UniValue name = find_value(response, "name");
            if (name.isStr()) {
                this->ui->deviceNameLabel->setText(QString::fromStdString(name.get_str()));
                if (tag == DBB_RESPONSE_TYPE_SET_DEVICE_NAME_CREATE)
                    getInfo();
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
            websocketServer->sendDataToClientInECDHParingState(response);
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
                //remove local wallets
                if (singleWallet)
                    singleWallet->client.RemoveLocalData();

                if (vMultisigWallets[0])
                    vMultisigWallets[0]->client.RemoveLocalData();

                resetInfos();
                getInfo();
            }
        }
        if (tag == DBB_RESPONSE_TYPE_ERASE) {
            UniValue resetObj = find_value(response, "reset");
            if (resetObj.isStr() && resetObj.get_str() == "success") {
                sessionPasswordDuringChangeProcess.clear();

                //remove local wallets
                singleWallet->client.RemoveLocalData();
                vMultisigWallets[0]->client.RemoveLocalData();

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
                setDeviceName(DBB_RESPONSE_TYPE_SET_DEVICE_NAME_CREATE);
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

            singleWallet->client.GetNewAddress(address, keypath);
            singleWallet->rewriteKeypath(keypath);
            emit walletAddressIsAvailable(this->singleWallet, address, keypath);

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
        return showAlert("Error", "Invalid amount");

    this->ui->sendToAddress->clearFocus();
    this->ui->sendAmount->clearFocus();
    DBBNetThread* thread = DBBNetThread::DetachThread();
    thread->currentThread = std::thread([this, thread, amount]() {
        UniValue proposalOut;
        std::string errorOut;

        int fee = singleWallet->client.GetFeeForPriority(0);

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
        showAlert(tr("Invalid Invitation"), tr("Your Copay Wallet Invitation is invalid"));
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
        QMessageBox::information(this, tr("Copay Wallet Response"), tr("Successfull joined Copay Wallet"), QMessageBox::Ok);
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
        //set the keys and try to join the wallet
        joinMultisigWalletInitiate(wallet);
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

    this->ui->noProposalsAvailable->setVisible(true);
}

void DBBDaemonGui::updateWallet(DBBWallet* wallet)
{
    if (wallet == singleWallet) {
        SingleWalletUpdateWallets();
    } else
        MultisigUpdateWallets();
}

void DBBDaemonGui::MultisigUpdateWallets()
{
    DBBWallet* wallet = vMultisigWallets[0];
    if (!wallet->client.IsSeeded())
        return;

    multisigWalletIsUpdating = true;
    executeNetUpdateWallet(wallet, true, [wallet, this](bool walletsAvailable, const std::string& walletsResponse) {
        emit getWalletsResponseAvailable(wallet, walletsAvailable, walletsResponse);
    });
}

void DBBDaemonGui::SingleWalletUpdateWallets(bool showLoading)
{
    if (singleWallet->updatingWallet)
    {
        singleWallet->shouldUpdateWalletAgain = true;
        return;
    }
    if (!singleWallet->client.IsSeeded())
        return;
    
    singleWalletIsUpdating = true;
    executeNetUpdateWallet(singleWallet, showLoading, [this](bool walletsAvailable, const std::string& walletsResponse) {
        emit getWalletsResponseAvailable(this->singleWallet, walletsAvailable, walletsResponse);
    });

    DBBNetThread* thread = DBBNetThread::DetachThread();
    thread->currentThread = std::thread([this, thread]() {
        std::string txHistoryResponse;
        bool transactionHistoryAvailable = this->singleWallet->client.GetTransactionHistory(txHistoryResponse);


        UniValue data;
        if (transactionHistoryAvailable)
            data.read(txHistoryResponse);

        emit getTransactionHistoryAvailable(this->singleWallet, transactionHistoryAvailable, data);
        thread->completed();
    });
}

void DBBDaemonGui::updateUIMultisigWallets(const UniValue& walletResponse)
{
    vMultisigWallets[0]->updateData(walletResponse);

    if (vMultisigWallets[0]->currentPaymentProposals.isArray()) {
        this->ui->proposalsLabel->setText(tr("Current Payment Proposals (%1)").arg(vMultisigWallets[0]->currentPaymentProposals.size()));
    }

    //TODO, add a monetary amount / unit helper function
    this->ui->multisigBalance->setText(QString::fromStdString(DBB::formatMoney(vMultisigWallets[0]->totalBalance)));
    this->ui->multisigWalletName->setText(QString::fromStdString(vMultisigWallets[0]->walletRemoteName));
}

void DBBDaemonGui::updateUISingleWallet(const UniValue& walletResponse)
{
    singleWallet->updateData(walletResponse);

    //TODO, add a monetary amount / unit helper function
    QString balance = QString::fromStdString(DBB::formatMoney(singleWallet->totalBalance));

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

    if (!historyAvailable || !history.isArray())
        return;

    transactionTableModel = new  QStandardItemModel(history.size(), 5, this);

    transactionTableModel->setHeaderData(0, Qt::Horizontal, QObject::tr("TXID"));
    transactionTableModel->setHeaderData(1, Qt::Horizontal, QObject::tr("Amount"));
    transactionTableModel->setHeaderData(2, Qt::Horizontal, QObject::tr("Address"));
    transactionTableModel->setHeaderData(3, Qt::Horizontal, QObject::tr("Date"));
    transactionTableModel->setHeaderData(4, Qt::Horizontal, QObject::tr(""));

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
        if (timeUV.isNum())
        {
            QDateTime timestamp;
            timestamp.setTime_t(timeUV.get_int64());
            QStandardItem *item = new QStandardItem(timestamp.toString(Qt::SystemLocaleShortDate));
            item->setToolTip(tr("Double-click for more details"));
            item->setFont(font);
            transactionTableModel->setItem(cnt, 3, item);
        }
            
        UniValue confirmsUV = find_value(obj, "confirmations");
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
            QStandardItem *item = new QStandardItem(QIcon(iconName), "");
            item->setToolTip(tooltip + tr(" confirmations"));
            item->setTextAlignment(Qt::AlignCenter); 
            transactionTableModel->setItem(cnt, 4, item);
        }

        UniValue txidUV = find_value(obj, "txid");
        if (txidUV.isStr())
        {
            QStandardItem *item = new QStandardItem(QString::fromStdString(txidUV.get_str()) );
            transactionTableModel->setItem(cnt, 0, item);
        }

        cnt++;
    }
  
    if (!cnt) {
        ui->tableWidget->setVisible(false);   
        return;
    }

    ui->tableWidget->setVisible(true);   
    ui->tableWidget->setModel(transactionTableModel);
    ui->tableWidget->setColumnHidden(0, true);
    ui->tableWidget->setColumnWidth(4, 0); // Trick to get column smaller than minimum width
                                           // when resized in setSectionResizeMode().
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
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
                std::unique_lock<std::recursive_mutex> lock(this->cs_walletObjects);
                wallet->shouldUpdateWalletAgain = false;
                walletsAvailable = wallet->client.GetWallets(walletsResponse);
                wallet->client.GetFeeLevels();
            }while(wallet->shouldUpdateWalletAgain);

            wallet->updatingWallet = false;
        }
        cmdFinished(walletsAvailable, walletsResponse);
        thread->completed();
    });

    if (showLoading)
        setNetLoading(true);
}

void DBBDaemonGui::parseWalletsResponse(DBBWallet* wallet, bool walletsAvailable, const std::string& walletsResponse)
{
    setNetLoading(false);

    if (wallet == singleWallet)
        singleWalletIsUpdating = false;
    else
        multisigWalletIsUpdating = false;

    UniValue response;
    if (response.read(walletsResponse)) {
        if (response.isObject()) {
            DBB::LogPrint("Got update wallet response...\n", "");

            if (wallet == singleWallet)
                updateUISingleWallet(response);
            else {
                updateUIMultisigWallets(response);
                MultisigUpdatePaymentProposals(response);
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

        printf("pending txps: %s", pendingTxps.write(2, 2).c_str());
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

            this->ui->noProposalsAvailable->setVisible(false);

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

    //build sign command
    std::string hashCmd;
    for (const std::pair<std::string, std::vector<unsigned char> >& hashAndPathPair : inputHashesAndPaths) {
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

    std::string hexHash = DBB::HexStr(&inputHashesAndPaths[0].second[0], &inputHashesAndPaths[0].second[0] + 32);

    std::string twoFaPart = "";
    if (!tfaCode.isEmpty())
        twoFaPart = "\"pin\" : \""+tfaCode.toStdString()+"\", ";

    uint8_t serTxHash[32];
    btc_hash((const uint8_t*)&serTx[0], serTx.size(), serTxHash);
    std::string serTxHashHex = DBB::HexStr(serTxHash, serTxHash+32);

    std::string command = "{\"sign\": { "+twoFaPart+"\"type\": \"meta\", \"meta\" : \""+serTxHashHex+"\", \"data\" : [ " + hashCmd + " ], \"checkpub\" : "+checkpubObj.write()+" } }";

    if (command.size() >= HID_REPORT_SIZE_DEFAULT)
    {
        DBB::LogPrint("signing size exceeded (to many inputs)\n", "");
        emit shouldShowAlert("Error", tr("To many inputs. Currently a max. of 18 inputs is supported"));
        return;
    }
    bool ret = false;
    DBB::LogPrint("Request signing...\n", "");
    executeCommandWrapper(command, DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [wallet, &ret, actionType, paymentProposal, inputHashesAndPaths, serTx, this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
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
                UniValue errorCodeObj = find_value(errorObj, "code");
                UniValue errorMessageObj = find_value(errorObj, "message");
                if (errorMessageObj.isStr())
                {
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

                        for (const UniValue& oneSig : vSignatureObjects) {
                            UniValue sigObject = find_value(oneSig, "sig");
                            UniValue pubKey = find_value(oneSig, "pubkey");
                            if (!sigObject.isNull() && sigObject.isStr()) {
                                sigs.push_back(sigObject.get_str());
                                //client.BroadcastProposal(values[0]);
                            }
                        }

                        emit shouldHideVerificationInfo();
                        emit signedProposalAvailable(wallet, paymentProposal, sigs);
                        ret = true;
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

#pragma mark - WebSocket Stack (ECDH)

void DBBDaemonGui::sendECDHPairingRequest(const std::string &pubkey)
{
    if (!deviceReadyToInteract)
        return;

    //:translation: accept pairing rquest message box
    QMessageBox::StandardButton reply = QMessageBox::question(this, "", tr("Would you like to accept a pairing request?"), QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::No)
        return;

    DBB::LogPrint("Paring request\n", "");

    executeCommandWrapper("{\"verifypass\": {\"ecdh\" : \"" + pubkey + "\"}}", DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_VERIFYPASS_ECDH);
    });
    showModalInfo("Pairing Verification Device");
}

void DBBDaemonGui::amountOfPairingDevicesChanged(int amountOfClients)
{
    DBB::LogPrint("Verification devices changed, new: %d\n", amountOfClients);
    this->statusBarVDeviceIcon->setToolTip(tr("%1 Verification Device(s) Connected").arg(amountOfClients));
    this->statusBarVDeviceIcon->setVisible((amountOfClients > 0));
}

#pragma mark - Update Check

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

bool DBBDaemonGui::SendRequest(const std::string& method,
                                     const std::string& url,
                                     const std::string& args,
                                     std::string& responseOut,
                                     long& httpcodeOut)
{
    CURL* curl;
    CURLcode res;

    bool success = false;

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if (curl) {
        struct curl_slist* chunk = NULL;
        chunk = curl_slist_append(chunk, "Content-Type: application/json");
        res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        if (method == "post")
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, args.c_str());

        if (method == "delete") {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, args.c_str());
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        }

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseOut);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

#if defined(__linux__) || defined(__unix__)
        //need to libcurl, load it once, set the CA path at runtime
        //we assume only linux needs CA fixing
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca_file.c_str());
#endif

#ifdef DBB_ENABLE_DEBUG
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
#endif

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            DBB::LogPrintDebug("curl_easy_perform() failed "+ ( curl_easy_strerror(res) ? std::string(curl_easy_strerror(res)) : ""), "");
            success = false;
        } else {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpcodeOut);
            success = true;
        }

        curl_slist_free_all(chunk);
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();

    DBB::LogPrintDebug("response: "+responseOut, "");
    return success;
};

void DBBDaemonGui::checkForUpdateInBackground()
{
    checkForUpdate(false);
}

void DBBDaemonGui::checkForUpdate(bool reportAlways)
{
    if (checkingForUpdates)
        return;

    DBBNetThread* thread = DBBNetThread::DetachThread();
    thread->currentThread = std::thread([this, thread, reportAlways]() {
        std::string response;
        long httpStatusCode;
        SendRequest("post", "https://digitalbitbox.com/dbb-app/update.json", "dv="+std::string(VERSION)+"&version="+std::string(DBB_PACKAGE_VERSION), response, httpStatusCode);
        emit checkForUpdateResponseAvailable(response, httpStatusCode, reportAlways);
        thread->completed();
    });

    checkingForUpdates = true;
}

void DBBDaemonGui::parseCheckUpdateResponse(const std::string &response, long statuscode, bool reportAlways)
{
    checkingForUpdates = false;

    UniValue jsonOut;
    jsonOut.read(response);

    bool updateAvailable = false;

    try {
        if (jsonOut.isObject())
        {
            UniValue subtext = find_value(jsonOut, "msg");
            UniValue url = find_value(jsonOut, "url");
            if (subtext.isStr() && url.isStr())
            {
                //:translation: accept pairing rquest message box
                QMessageBox::StandardButton reply = QMessageBox::question(this, "", QString::fromStdString(subtext.get_str()), QMessageBox::Yes | QMessageBox::No);
                if (reply == QMessageBox::Yes)
                {
                    updateAvailable = true;
                    QString link = QString::fromStdString(url.get_str());
                    QDesktopServices::openUrl(QUrl(link));
                    return;
                }
            }
        }
    } catch (std::exception &e) {
        DBB::LogPrint("Error while reading update json\n", "");
    }

    if (!updateAvailable && reportAlways)
    {
        showModalInfo(tr("You are up-to-date."), DBB_PROCESS_INFOLAYER_CONFIRM_WITH_BUTTON);
    }
}

static const char *ca_paths[] = {
    "/etc/ssl/certs/ca-certificates.crt", // Debian/Ubuntu/Gentoo etc.
    "/etc/ssl/certs/ca-bundle.crt",       // Debian/Ubuntu/Gentoo etc.
    "/etc/pki/tls/certs/ca-bundle.crt",   // Fedora/RHEL
    "/etc/ssl/ca-bundle.pem",             // OpenSUSE
    "/etc/pki/tls/cacert.pem",            // OpenELEC
};

inline bool file_exists (const char *name) {
    struct stat buffer;
    int result = stat(name, &buffer);
    return (result == 0);
}

std::string DBBDaemonGui::getCAFile()
{
    size_t i = 0;
    for( i = 0; i < sizeof(ca_paths) / sizeof(ca_paths[0]); i++)
    {
        if (file_exists(ca_paths[i]))
        {
            return std::string(ca_paths[i]);
        }
    }
    return "";
}
