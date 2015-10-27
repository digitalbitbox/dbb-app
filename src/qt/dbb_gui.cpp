// Copyright (c) 2015 Jonas Schnelli
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "dbb_gui.h"

#include <QAction>
#include <QApplication>
#include <QPushButton>
#include <QDebug>
#include <QInputDialog>
#include <QMessageBox>
#include <QSpacerItem>
#include <QToolBar>

#include "ui/ui_overview.h"
#include "seeddialog.h"
#include <dbb.h>

#include "dbb_util.h"

#include <cstdio>
#include <ctime>
#include <functional>

#include <univalue.h>
#include <btc/bip32.h>
#include <btc/tx.h>


//TODO: move to util:
std::vector<std::string> &split(const std::string &s, char delim, std::vector<std::string> &elems) {
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}


std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> elems;
    split(s, delim, elems);
    return elems;
}
/////////////////

void executeCommand(const std::string& cmd, const std::string& password, std::function<void(const std::string&, dbb_cmd_execution_status_t status)> cmdFinished);

bool DBBDaemonGui::QTexecuteCommandWrapper(const std::string& cmd, const dbb_process_infolayer_style_t layerstyle, std::function<void(const std::string&, dbb_cmd_execution_status_t status)> cmdFinished) {

    if (processComnand)
        return false;

    if (layerstyle == DBB_PROCESS_INFOLAYER_STYLE_TOUCHBUTTON)
    {
            ui->touchbuttonInfo->setVisible(true);
    }

    setLoading(true);
    processComnand = true;
    executeCommand(cmd, sessionPassword, cmdFinished);

    return true;
}


DBBDaemonGui::DBBDaemonGui(QWidget* parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    overviewAction(0),
    walletsAction(0),
    settingsAction(0),
    statusBarButton(0),
    statusBarLabelRight(0),
    statusBarLabelLeft(0),
    backupDialog(0),
    processComnand(0),
    deviceConnected(0),
    cachedWalletAvailableState(0)
{
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    
    ui->setupUi(this);
    ui->touchbuttonInfo->setVisible(false);
    // set light transparent background for touch button info layer
    this->ui->touchbuttonInfo->setStyleSheet("background-color: rgba(255, 255, 255, 240);");

    // allow serval signaling data types
    qRegisterMetaType<UniValue>("UniValue");
    qRegisterMetaType<dbb_cmd_execution_status_t>("dbb_cmd_execution_status_t");
    qRegisterMetaType<dbb_response_type_t>("dbb_response_type_t");
    qRegisterMetaType<std::vector<std::string>>("std::vector<std::string>");

    // connect UI
    connect(ui->eraseButton, SIGNAL(clicked()), this, SLOT(eraseClicked()));
    connect(ui->ledButton, SIGNAL(clicked()), this, SLOT(ledClicked()));
    connect(ui->passwordButton, SIGNAL(clicked()), this, SLOT(setPasswordClicked()));
    connect(ui->seedButton, SIGNAL(clicked()), this, SLOT(seed()));
    connect(ui->createWallet, SIGNAL(clicked()), this, SLOT(seed()));
    connect(ui->joinCopayWallet, SIGNAL(clicked()), this, SLOT(JoinCopayWallet()));
    connect(ui->checkProposals, SIGNAL(clicked()), this, SLOT(checkPaymentProposals()));
    connect(ui->showBackups, SIGNAL(clicked()), this, SLOT(showBackupDialog()));
    connect(ui->getRand, SIGNAL(clicked()), this, SLOT(getRandomNumber()));
    connect(ui->lockDevice, SIGNAL(clicked()), this, SLOT(lockDevice()));

    // connect custom signals
    connect(this, SIGNAL(showCommandResult(const QString&)), this, SLOT(setResultText(const QString&)));
    connect(this, SIGNAL(deviceStateHasChanged(bool)), this, SLOT(changeConnectedState(bool)));
    connect(this, SIGNAL(XPubForCopayWalletIsAvailable()), this, SLOT(GetRequestXPubKey()));
    connect(this, SIGNAL(RequestXPubKeyForCopayWalletIsAvailable()), this, SLOT(JoinCopayWalletWithXPubKey()));
    connect(this, SIGNAL(gotResponse(const UniValue&, dbb_cmd_execution_status_t, dbb_response_type_t)), this, SLOT(parseResponse(const UniValue&, dbb_cmd_execution_status_t, dbb_response_type_t)));
    connect(this, SIGNAL(shouldVerifySigning(const QString&)), this, SLOT(showEchoVerification(const QString&)));
    connect(this, SIGNAL(signedProposalAvailable(const UniValue&, const std::vector<std::string> &)), this, SLOT(postSignedPaymentProposal(const UniValue&, const std::vector<std::string> &)));

    // create backup dialog instance
    backupDialog = new BackupDialog(0);
    connect(backupDialog, SIGNAL(addBackup()), this, SLOT(addBackup()));
    connect(backupDialog, SIGNAL(eraseAllBackups()), this, SLOT(eraseAllBackups()));
    connect(backupDialog, SIGNAL(restoreFromBackup(const QString&)), this, SLOT(restoreBackup(const QString&)));


    //set window icon
    QApplication::setWindowIcon(QIcon(":/icons/dbb"));
    setWindowTitle(tr("The Digital Bitbox"));

    //set status bar
    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    spacer->setMinimumWidth(3);
    spacer->setMaximumHeight(10);
    statusBar()->addWidget(spacer);
    this->statusBarButton = new QPushButton(QIcon(":/icons/connected"), "");
    this->statusBarButton->setEnabled(false);
    this->statusBarButton->setFlat(true);
    this->statusBarButton->setMaximumWidth(18);
    this->statusBarButton->setMaximumHeight(18);
    this->statusBarButton->setVisible(false);
    statusBar()->addWidget(this->statusBarButton);

    this->statusBarLabelLeft = new QLabel(tr("No Device Found"));
    statusBar()->addWidget(this->statusBarLabelLeft);

    this->statusBarLabelRight = new QLabel("");
    statusBar()->addPermanentWidget(this->statusBarLabelRight);

    // tabbar
    QActionGroup *tabGroup = new QActionGroup(this);
    overviewAction = new QAction(QIcon(":/icons/home").pixmap(32), tr("&Overview"), this);
        overviewAction->setToolTip(tr("Show general overview of wallet"));
        overviewAction->setCheckable(true);
        overviewAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_1));
    tabGroup->addAction(overviewAction);
    
    walletsAction = new QAction(QIcon(":/icons/copay"), tr("&Wallets"), this);
        walletsAction->setToolTip(tr("Show Copay wallet screen"));
        walletsAction->setCheckable(true);
        walletsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_2));
    tabGroup->addAction(walletsAction);
    
    settingsAction = new QAction(QIcon(":/icons/settings"), tr("&Experts"), this);
        settingsAction->setToolTip(tr("Show Settings wallet screen"));
        settingsAction->setCheckable(true);
        settingsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_3));
    tabGroup->addAction(settingsAction);
     
    QToolBar *toolbar = addToolBar(tr("Tabs toolbar"));
            toolbar->setMovable(false);
            toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            toolbar->addAction(overviewAction);
            toolbar->addAction(walletsAction);
            toolbar->addAction(settingsAction);
            overviewAction->setChecked(true);
    toolbar->setStyleSheet("QToolButton{padding: 3px; font-size:11pt;}");
    toolbar->setIconSize(QSize(24,24));
    
    connect(overviewAction, SIGNAL(triggered()), this, SLOT(gotoOverviewPage()));
    connect(walletsAction, SIGNAL(triggered()), this, SLOT(gotoMultisigPage()));
    connect(settingsAction, SIGNAL(triggered()), this, SLOT(gotoSettingsPage()));

    //load local pubkeys
    DBBMultisigWallet copayWallet;
    copayWallet.client.LoadLocalData();
    vMultisigWallets.push_back(copayWallet);

    deviceConnected = false;
    resetInfos();
    //set status bar connection status
    checkDevice();
    changeConnectedState(DBB::isConnectionOpen());


    processComnand = false;
}

DBBDaemonGui::~DBBDaemonGui()
{

}

/*
 /////////////////////////////
 Plug / Unplug / GetInfo stack
 /////////////////////////////
*/
void DBBDaemonGui::changeConnectedState(bool state)
{
    bool stateChanged = deviceConnected != state;
    if (stateChanged)
    {
        if (state) {
            deviceConnected = true;
            this->statusBarLabelLeft->setText("Device Connected");
            this->statusBarButton->setVisible(true);
        } else {
            deviceConnected = false;
            this->statusBarLabelLeft->setText("No Device Found");
            this->statusBarButton->setVisible(false);
        }

        checkDevice();
    }
}

void DBBDaemonGui::checkDevice()
{
    this->ui->verticalLayoutWidget->setVisible(deviceConnected);
    this->ui->noDeviceWidget->setVisible(!deviceConnected);

    if (!deviceConnected)
    {
        walletsAction->setEnabled(false);
        settingsAction->setEnabled(false);
        gotoOverviewPage();
        overviewAction->setChecked(true);
        resetInfos();
        sessionPassword.clear();
    }
    else
    {
        walletsAction->setEnabled(true);
        settingsAction->setEnabled(true);
        askForSessionPassword();
        getInfo();
    }
}

void DBBDaemonGui::setLoading(bool status)
{
    if (!status)
        ui->touchbuttonInfo->setVisible(false);

    this->statusBarLabelRight->setText((status) ? "processing..." : "");
    //TODO, subclass label and make it animated
}

void DBBDaemonGui::resetInfos()
{
    this->ui->versionLabel->setText("loading info...");
    this->ui->nameLabel->setText("loading info...");

    updateOverviewFlags(false,false,true);
}


/*
 /////////////////
 UI Action Stack
 /////////////////
*/
void DBBDaemonGui::gotoOverviewPage()
{
    this->ui->stackedWidget->setCurrentIndex(0);
}

void DBBDaemonGui::gotoMultisigPage()
{
    this->ui->stackedWidget->setCurrentIndex(1);
}

void DBBDaemonGui::gotoSettingsPage()
{
    this->ui->stackedWidget->setCurrentIndex(2);
}

void DBBDaemonGui::showEchoVerification(QString echoStr)
{
    QMessageBox::information(this, tr("Verify"),
                             tr("ToDo Verify (%1)").arg(echoStr),
                             QMessageBox::Ok);
}

void DBBDaemonGui::askForSessionPassword()
{
    //ask for session password
    bool ok;
    QString text = QInputDialog::getText(this, tr("Start Session"), tr("Current Password"), QLineEdit::Normal, "", &ok);
    if (ok && !text.isEmpty()) {
        sessionPassword = text.toStdString();
    }
}

//TODO: remove, not direct json result text in UI, add log
void DBBDaemonGui::setResultText(const QString& result)
{
    processComnand = false;
    qDebug() << "SetResultText Called\n";
    this->statusBarLabelRight->setText("");
}

void DBBDaemonGui::updateOverviewFlags(bool walletAvailable, bool lockAvailable, bool loading)
{
    this->ui->walletCheckmark->setIcon(QIcon(walletAvailable ? ":/icons/okay" : ":/icons/warning"));
    this->ui->walletLabel->setText(tr(walletAvailable ? "Wallet available" : "No Wallet"));
    this->ui->createWallet->setVisible(!walletAvailable);

    this->ui->lockCheckmark->setIcon(QIcon(lockAvailable ? ":/icons/okay" : ":/icons/warning"));
    this->ui->lockLabel->setText(lockAvailable ? "Device 2FA Lock" : "No 2FA set");

    if (loading)
    {
        this->ui->lockLabel->setText("loading info...");
        this->ui->walletLabel->setText("loading info...");

        this->ui->walletCheckmark->setIcon(QIcon(":/icons/warning")); //TODO change to loading...
        this->ui->lockCheckmark->setIcon(QIcon(":/icons/warning")); //TODO change to loading...
    }
}

/*
 //////////////////////////
 DBB USB Commands (General)
 //////////////////////////
*/

//TODO: remove sendCommand method
bool DBBDaemonGui::sendCommand(const std::string& cmd, const std::string& password, dbb_response_type_t tag)
{
    //ensure we don't fill the queue
    //at the moment the UI should only post one command into the queue

    if (processComnand) {
        qDebug() << "Already processing a command\n";
        return false;
    }
    processComnand = true;
    QTexecuteCommandWrapper(cmd, DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this, tag](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        //send a signal to the main thread
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, tag);
    });
    return true;
}

void DBBDaemonGui::eraseClicked()
{
    if (QTexecuteCommandWrapper("{\"reset\":\"__ERASE__\"}", DBB_PROCESS_INFOLAYER_STYLE_TOUCHBUTTON, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
            UniValue jsonOut;
            jsonOut.read(cmdOut);
            emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_ERASE);
        }))
    {
        vMultisigWallets[0].client.RemoveLocalData();
        sessionPasswordDuringChangeProcess = sessionPassword;
        sessionPassword.clear();
    }
}

void DBBDaemonGui::ledClicked()
{
    QTexecuteCommandWrapper("{\"led\" : \"toggle\"}", DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);

        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_LED_BLINK);
    });
}

void DBBDaemonGui::getInfo()
{
    QTexecuteCommandWrapper("{\"device\":\"info\"}", DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_INFO);
    });
}

void DBBDaemonGui::setPasswordClicked(bool showInfo)
{
    bool ok;
    QString text = QInputDialog::getText(this, tr("Set New Password"), tr("Password"), QLineEdit::Normal, "0000", &ok);
    if (ok && !text.isEmpty()) {
        std::string command = "{\"password\" : \"" + text.toStdString() + "\"}";

        if (QTexecuteCommandWrapper(command, (showInfo) ? DBB_PROCESS_INFOLAYER_STYLE_TOUCHBUTTON : DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
            UniValue jsonOut;
            jsonOut.read(cmdOut);
            emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_PASSWORD);
        }))
        {
            sessionPasswordDuringChangeProcess = sessionPassword;
            sessionPassword = text.toStdString();
        }
    }

}

void DBBDaemonGui::seed()
{
    std::string command = "{\"seed\" : {\"source\" :\"create\","
    "\"decrypt\": \"no\","
    "\"salt\" : \"\"} }";

    QTexecuteCommandWrapper(command, (cachedWalletAvailableState) ? DBB_PROCESS_INFOLAYER_STYLE_TOUCHBUTTON : DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_CREATE_WALLET);
    });
}

/*
 /////////////////
 Utils
 /////////////////
*/
void DBBDaemonGui::getRandomNumber()
{

    std::string command = "{\"random\" : \"true\" }";

    QTexecuteCommandWrapper(command, DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_RANDOM_NUM);
    });
}

void DBBDaemonGui::lockDevice()
{

    std::string command = "{\"device\" : \"lock\" }";

    QTexecuteCommandWrapper(command, DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_DEVICE_LOCK);
    });
}

/*
 /////////////////
 Backup Stack
 /////////////////
*/
void DBBDaemonGui::showBackupDialog()
{
    backupDialog->show();
    listBackup();
}

void DBBDaemonGui::addBackup()
{

    std::time_t rawtime;
    std::tm* timeinfo;
    char buffer [80];

    std::time(&rawtime);
    timeinfo = std::localtime(&rawtime);

    std::strftime(buffer,80,"%Y-%m-%d-%H-%M-%S",timeinfo);
    std::string timeStr(buffer);

    std::string command = "{\"backup\" : {\"encrypt\" :\"no\","
    "\"filename\": \"backup-"+timeStr+".bak\"} }";

    QTexecuteCommandWrapper(command, DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_ADD_BACKUP);
    });
}

void DBBDaemonGui::listBackup()
{
    std::string command = "{\"backup\" : \"list\" }";

    QTexecuteCommandWrapper(command, DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_LIST_BACKUP);
    });

    backupDialog->showLoading();
}

void DBBDaemonGui::eraseAllBackups()
{
    QMessageBox::StandardButton reply = QMessageBox::question(this, tr("Erase All Backups?"), tr("Are your sure you want to erase all backups"), QMessageBox::Yes|QMessageBox::No);
    if (reply == QMessageBox::No)
        return;

    std::string command = "{\"backup\" : \"erase\" }";

    QTexecuteCommandWrapper(command, DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_ERASE_BACKUP);
    });

    backupDialog->showLoading();
}

void DBBDaemonGui::restoreBackup(const QString& backupFilename)
{
    std::string command = "{\"seed\" : {\"source\" :\""+backupFilename.toStdString()+"\","
    "\"decrypt\": \"no\","
    "\"salt\" : \"\"} }";

    QTexecuteCommandWrapper(command, DBB_PROCESS_INFOLAYER_STYLE_TOUCHBUTTON, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_CREATE_WALLET);
    });
    
    backupDialog->close();
}

/*
 ///////////////////////////////////
 DBB USB Commands (Response Parsing)
 ///////////////////////////////////
*/
void DBBDaemonGui::parseResponse(const UniValue &response, dbb_cmd_execution_status_t status, dbb_response_type_t tag)
{
    processComnand = false;
    setLoading(false);

    if (response.isObject())
    {
        UniValue errorObj = find_value(response, "error");
        UniValue touchbuttonObj = find_value(response, "touchbutton");
        bool touchErrorShowed = false;

        if (touchbuttonObj.isStr())
        {
            QMessageBox::information(this, tr("Touchbutton"), QString::fromStdString(touchbuttonObj.get_str()), QMessageBox::Ok);
            touchErrorShowed = true;
        }

        if (errorObj.isObject())
        {
            //error found
            UniValue errorCodeObj = find_value(errorObj, "code");
            UniValue errorMessageObj = find_value(errorObj, "message");
            if (errorCodeObj.isNum() && errorCodeObj.get_int() == 108)
            {
                //password wrong
                QMessageBox::warning(this, tr("Password Error"), tr("Password Wrong. %1").arg(QString::fromStdString(errorMessageObj.get_str())), QMessageBox::Ok);

                //try again
                askForSessionPassword();
                getInfo();
            }
            else if (errorCodeObj.isNum() && errorCodeObj.get_int() == 110)
            {
                //password wrong
                QMessageBox::critical(this, tr("Password Error"), tr("Device Reset. %1").arg(QString::fromStdString(errorMessageObj.get_str())), QMessageBox::Ok);
            }
            else if (errorCodeObj.isNum() && errorCodeObj.get_int() == 101)
            {
                //password wrong
                QMessageBox::warning(this, tr("Password Error"), QString::fromStdString(errorMessageObj.get_str()), QMessageBox::Ok);

                sessionPassword.clear();
                setPasswordClicked(false);
            }
            else
            {
                //password wrong
                QMessageBox::warning(this, tr("Error"), QString::fromStdString(errorMessageObj.get_str()), QMessageBox::Ok);
            }
        }
        else if (tag == DBB_RESPONSE_TYPE_INFO)
        {
            UniValue deviceObj = find_value(response, "device");
            if (deviceObj.isObject())
            {
                UniValue version = find_value(deviceObj, "version");
                UniValue name = find_value(deviceObj, "name");
                UniValue seeded = find_value(deviceObj, "seeded");
                UniValue lock = find_value(deviceObj, "lock");
                bool walletAvailable = (seeded.isBool() && seeded.isTrue());
                bool lockAvailable = (lock.isBool() && lock.isTrue());

                if (version.isStr())
                    this->ui->versionLabel->setText(QString::fromStdString(version.get_str()));
                if (name.isStr())
                    this->ui->nameLabel->setText(QString::fromStdString(name.get_str()));

                updateOverviewFlags(walletAvailable,lockAvailable,false);
            }
        }
        else if (tag == DBB_RESPONSE_TYPE_CREATE_WALLET)
        {
            UniValue touchbuttonObj = find_value(response, "touchbutton");
            UniValue seedObj = find_value(response, "seed");
            UniValue errorObj = find_value(response, "error");
            QString errorString;

            if (errorObj.isObject())
            {
                UniValue errorMsgObj = find_value(errorObj, "message");
                if (errorMsgObj.isStr())
                    errorString = QString::fromStdString(errorMsgObj.get_str());
            }
            if (!touchbuttonObj.isNull() && touchbuttonObj.isObject())
            {
                UniValue errorObj = find_value(touchbuttonObj, "error");
                if (!errorObj.isNull() && errorObj.isStr())
                    errorString = QString::fromStdString(errorObj.get_str());
            }
            if (!seedObj.isNull() && seedObj.isStr() && seedObj.get_str() == "success")
            {
                QMessageBox::information(this, tr("Wallet Created"), tr("Your wallet has been created successfully!"), QMessageBox::Ok);
                getInfo();
            }
            else
            {
                if (!touchErrorShowed)
                    QMessageBox::warning(this, tr("Wallet Error"), errorString, QMessageBox::Ok);
            }
        }
        else if (tag == DBB_RESPONSE_TYPE_PASSWORD)
        {
            UniValue passwordObj = find_value(response, "password");
            if (status != DBB_CMD_EXECUTION_STATUS_OK || (passwordObj.isStr() && passwordObj.get_str() == "success"))
            {
                sessionPasswordDuringChangeProcess.clear();

                //could not decrypt, password was changed successfully
                QMessageBox::information(this, tr("Password Set"), tr("Password has been set successfully!"), QMessageBox::Ok);
                getInfo();
            }
            else {
                QString errorString;
                UniValue touchbuttonObj = find_value(response, "touchbutton");
                if (!touchbuttonObj.isNull() && touchbuttonObj.isObject())
                {
                    UniValue errorObj = find_value(touchbuttonObj, "error");
                    if (!errorObj.isNull() && errorObj.isStr())
                        errorString = QString::fromStdString(errorObj.get_str());
                }

                //reset password in case of an error
                sessionPassword = sessionPasswordDuringChangeProcess;
                sessionPasswordDuringChangeProcess.clear();

                QMessageBox::warning(this, tr("Password Error"), tr("Could not set password (error: %1)!").arg(errorString), QMessageBox::Ok);
            }
        }
        else if(tag == DBB_RESPONSE_TYPE_XPUB_MS_MASTER)
        {
            UniValue xPubKeyUV = find_value(response, "xpub");
            QString errorString;

            if (!xPubKeyUV.isNull() && xPubKeyUV.isStr())
            {

                btc_hdnode node;
                bool r = btc_hdnode_deserialize(xPubKeyUV.get_str().c_str(), &btc_chain_main, &node);

                char outbuf[112];
                btc_hdnode_serialize_public(&node, &btc_chain_test, outbuf, sizeof(outbuf));

                std::string xPubKeyNew(outbuf);

                vMultisigWallets[0].client.setMasterPubKey(xPubKeyNew);
                emit XPubForCopayWalletIsAvailable();
            }
            else
            {
                if (xPubKeyUV.isObject())
                {
                    UniValue errorObj = find_value(xPubKeyUV, "error");
                    if (!errorObj.isNull() && errorObj.isStr())
                        errorString = QString::fromStdString(errorObj.get_str());
                }

                QMessageBox::warning(this, tr("Join Wallet Error"), tr("Error joining Copay Wallet (%1)").arg(errorString), QMessageBox::Ok);
            }
        }
        else if(tag == DBB_RESPONSE_TYPE_XPUB_MS_REQUEST)
        {
            UniValue requestXPubKeyUV = find_value(response, "xpub");
            QString errorString;
            
            if (!requestXPubKeyUV.isNull() && requestXPubKeyUV.isStr())
            {
                btc_hdnode node;
                bool r = btc_hdnode_deserialize(requestXPubKeyUV.get_str().c_str(), &btc_chain_main, &node);

                char outbuf[112];
                btc_hdnode_serialize_public(&node, &btc_chain_test, outbuf, sizeof(outbuf));

                std::string xRequestKeyNew(outbuf);

                vMultisigWallets[0].client.setRequestPubKey(xRequestKeyNew);

                emit RequestXPubKeyForCopayWalletIsAvailable();
            }
            else
            {
                if (requestXPubKeyUV.isObject())
                {
                    UniValue errorObj = find_value(requestXPubKeyUV, "error");
                    if (!errorObj.isNull() && errorObj.isStr())
                        errorString = QString::fromStdString(errorObj.get_str());
                }

                QMessageBox::warning(this, tr("Join Wallet Error"), tr("Error joining Copay Wallet (%1)").arg(errorString), QMessageBox::Ok);
            }
        }
        else if(tag == DBB_RESPONSE_TYPE_ERASE)
        {
            UniValue resetObj = find_value(response, "reset");
            if (resetObj.isStr() && resetObj.get_str() == "success")
            {
                QMessageBox::information(this, tr("Erase"), tr("Device was erased successfully"), QMessageBox::Ok);
                sessionPasswordDuringChangeProcess.clear();

                resetInfos();
                getInfo();
            }
            else
            {
                //reset password in case of an error
                sessionPassword = sessionPasswordDuringChangeProcess;
                sessionPasswordDuringChangeProcess.clear();

                if (!touchErrorShowed)
                    QMessageBox::warning(this, tr("Erase error"), tr("Could not reset device"), QMessageBox::Ok);
            }
        }
        else if(tag == DBB_RESPONSE_TYPE_LIST_BACKUP && backupDialog)
        {
            UniValue backupObj = find_value(response, "backup");
            if (backupObj.isStr())
            {
                std::vector<std::string> data = split(backupObj.get_str(), ',');
                backupDialog->showList(data);
            }
        }
        else if(tag == DBB_RESPONSE_TYPE_ADD_BACKUP && backupDialog)
        {
            listBackup();
        }
        else if(tag == DBB_RESPONSE_TYPE_ERASE_BACKUP && backupDialog)
        {
            listBackup();
        }
        else if(tag == DBB_RESPONSE_TYPE_RANDOM_NUM)
        {
            UniValue randomNumObj = find_value(response, "random");
            if (randomNumObj.isStr())
            {
                QMessageBox::information(this, tr("Random Number"), QString::fromStdString(randomNumObj.get_str()), QMessageBox::Ok);
            }
        }
        else if(tag == DBB_RESPONSE_TYPE_DEVICE_LOCK)
        {
            bool suc = false;

            //check device:lock response and give appropriate user response
            UniValue deviceObj = find_value(response, "device");
            if (deviceObj.isObject())
            {
                UniValue lockObj = find_value(deviceObj, "lock");
                if (lockObj.isBool() && lockObj.isTrue())
                    suc = true;

            }
            if (suc)
                QMessageBox::information(this, tr("Success"), tr("Your device is now locked"), QMessageBox::Ok);
            else
                QMessageBox::warning(this, tr("Error"), tr("Could not lock your device"), QMessageBox::Ok);

            //reload device infos
            resetInfos();
            getInfo();
        }
        else
        {

        }
    }
}

/*
/////////////////
copay stack
/////////////////
*/
void DBBDaemonGui::JoinCopayWallet()
{
    setResultText(QString::fromStdString(""));

    if (!vMultisigWallets[0].client.IsSeeded()) {
        //if there is no xpub and request key, seed over DBB
        setResultText("Initializing Copay Client... this might take some seconds.");
        GetXPubKey();
    } else {
        //send a join request
        _JoinCopayWallet();
    }
}

void DBBDaemonGui::_JoinCopayWallet()
{
    bool ok;
    QString text = QInputDialog::getText(this, tr("Join Copay Wallet"), tr("Wallet Invitation Code"), QLineEdit::Normal, "", &ok);
    if (!ok || text.isEmpty())
        return;

    std::string result;
    bool ret = vMultisigWallets[0].client.JoinWallet("digitalbitbox", text.toStdString(), result);

    if (!ret) {
        UniValue responseJSON;
        std::string additionalErrorText = "unknown";
        if (responseJSON.read(result)) {
            UniValue errorText;
            errorText = find_value(responseJSON, "message");
            if (!errorText.isNull() && errorText.isStr())
                additionalErrorText = errorText.get_str();
        }
        setResultText(QString::fromStdString(result));

        int ret = QMessageBox::warning(this, tr("Copay Wallet Response"), tr("Joining the wallet failed (%1)").arg(QString::fromStdString(additionalErrorText)), QMessageBox::Ok);
    } else {
        QMessageBox::information(this, tr("Copay Wallet Response"), tr("Successfull joined Copay Wallet"), QMessageBox::Ok);
    }
}

void DBBDaemonGui::GetXPubKey()
{
    QTexecuteCommandWrapper("{\"xpub\":\"" + vMultisigWallets[0].baseKeyPath + "/45'\"}", DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_XPUB_MS_MASTER);
    });
}

void DBBDaemonGui::GetRequestXPubKey()
{
    //try to get the xpub for seeding the request private key (ugly workaround)
    //we cannot export private keys from a hardware wallet
    QTexecuteCommandWrapper("{\"xpub\":\"" + vMultisigWallets[0].baseKeyPath + "/1'/0\"}", DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
        UniValue jsonOut;
        jsonOut.read(cmdOut);
        emit gotResponse(jsonOut, status, DBB_RESPONSE_TYPE_XPUB_MS_REQUEST);
    });
}

void DBBDaemonGui::JoinCopayWalletWithXPubKey()
{
    //set the keys and try to join the wallet
    _JoinCopayWallet();
}


bool DBBDaemonGui::checkPaymentProposals()
{
    bool ret = false;
    int copayerIndex = INT_MAX;

    std::string walletsResponse;
    bool walletsAvailable = vMultisigWallets[0].client.GetWallets(walletsResponse);

    if (!walletsAvailable)
    {
        QMessageBox::warning(this, tr("No Wallet"),
                             tr("No Copay Wallet Available"),
                             QMessageBox::Ok);
    }

    UniValue response;
    if (response.read(walletsResponse)) {
        if (response.isObject()) {
            printf("Wallet: %s\n", response.write(true, 2).c_str());

            std::string currentXPub = vMultisigWallets[0].client.GetXPubKey();
            UniValue wallet = find_value(response, "wallet");
            UniValue copayers = find_value(wallet, "copayers");
            for (const UniValue& copayer : copayers.getValues()) {
                UniValue copayerXPub = find_value(copayer, "xPubKey");
                if (!copayerXPub.isNull()) {
                    if (currentXPub == copayerXPub.get_str()) {
                        UniValue addressManager = find_value(copayer, "addressManager");
                        UniValue copayerIndexObject = find_value(addressManager, "copayerIndex");
                        copayerIndex = copayerIndexObject.get_int();
                    }
                }
            }

            UniValue pendingTxps;
            pendingTxps = find_value(response, "pendingTxps");
            if (!pendingTxps.isNull() && pendingTxps.isArray()) {
                printf("pending txps: %s", pendingTxps.write(2, 2).c_str());
                std::vector<UniValue> values = pendingTxps.getValues();
                if (values.size() == 0)
                    return false;

                bool ok;

                QString amount;
                QString toAddress;

                UniValue toAddressUni = find_value(values[0], "toAddress");
                UniValue amountUni = find_value(values[0], "amount");
                if (toAddressUni.isStr())
                    toAddress = QString::fromStdString(toAddressUni.get_str());
                if (amountUni.isNum())
                    amount = QString::number(((double)amountUni.get_int64()/100000000.0));

                QMessageBox::StandardButton reply = QMessageBox::question(this, tr("Payment Proposal Available"), tr("Do you want to sign: pay %1BTC to %2").arg(amount, toAddress), QMessageBox::Yes|QMessageBox::No);
                if (reply == QMessageBox::No)
                    return false;

                std::vector<std::pair<std::string, std::vector<unsigned char> > > inputHashesAndPaths;
                vMultisigWallets[0].client.ParseTxProposal(values[0], inputHashesAndPaths);

                std::string hexHash = DBB::HexStr(&inputHashesAndPaths[0].second[0], &inputHashesAndPaths[0].second[0]+32);

                std::string command = "{\"sign\": { \"type\": \"hash\", \"data\" : \"" + hexHash + "\", \"keypath\" : \"" + vMultisigWallets[0].baseKeyPath + "/45'/" + inputHashesAndPaths[0].first + "\" }}";
                //printf("Command: %s\n", command.c_str());

                command = "{\"sign\": { \"type\": \"meta\", \"meta\" : \"somedata\", \"data\" : [ { \"hash\" : \"" + hexHash + "\", \"keypath\" : \"" + vMultisigWallets[0].baseKeyPath + "/45'/" + inputHashesAndPaths[0].first + "\" } ] } }";
                printf("Command: %s\n", command.c_str());

                QTexecuteCommandWrapper(command, DBB_PROCESS_INFOLAYER_STYLE_NO_INFO, [&ret, values, inputHashesAndPaths, this](const std::string& cmdOut, dbb_cmd_execution_status_t status) {
                    //send a signal to the main thread
                    processComnand = false;
                    
                    printf("cmd back: %s\n", cmdOut.c_str());
                    UniValue jsonOut(UniValue::VOBJ);
                    jsonOut.read(cmdOut);

                    UniValue echoStr = find_value(jsonOut, "echo");
                    if (!echoStr.isNull() && echoStr.isStr())
                    {

                        emit shouldVerifySigning(QString::fromStdString(echoStr.get_str()));
                    }
                    else
                    {
                        UniValue signObject = find_value(jsonOut, "sign");
                        if (signObject.isArray()) {
                            std::vector<UniValue> vSignatureObjects;
                            vSignatureObjects = signObject.getValues();
                            if (vSignatureObjects.size() > 0) {
                                UniValue sigObject = find_value(vSignatureObjects[0], "sig");
                                UniValue pubKey = find_value(vSignatureObjects[0], "pubkey");
                                if (!sigObject.isNull() && sigObject.isStr())
                                {
                                    //TODO: verify signature

                                    std::vector<std::string> sigs;
                                    sigs.push_back(sigObject.get_str());
                                    emit signedProposalAvailable(values[0], sigs);
                                    ret = true;
                                    //client.BroadcastProposal(values[0]);
                                }
                            }
                        }

                    }
                });
            }
        }
    }
    return ret;
}

void DBBDaemonGui::postSignedPaymentProposal(const UniValue& proposal, const std::vector<std::string> &vSigs)
{
    vMultisigWallets[0].client.PostSignaturesForTxProposal(proposal, vSigs);
}