// Copyright (c) 2015 Jonas Schnelli
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DBBDAEMON_QT_DAEMONGUI_H
#define DBBDAEMON_QT_DAEMONGUI_H

#include <QMainWindow>
#include <QWidget>
#include <QLabel>
#include <QPushButton>

#include "libbitpay-wallet-client/bpwalletclient.h"
#include "dbb_app.h"
#include "backupdialog.h"

namespace Ui
{
class MainWindow;
}

class DBBMultisigWallet
{
public:
    BitPayWalletClient client;
    std::string baseKeyPath;
    DBBMultisigWallet()
    {
        baseKeyPath = "m/112'";
    }
};

//DBB USB response types
typedef enum DBB_RESPONSE_TYPE
{
    DBB_RESPONSE_TYPE_UNKNOWN,
    DBB_RESPONSE_TYPE_PASSWORD,
    DBB_RESPONSE_TYPE_XPUB_MS_MASTER,
    DBB_RESPONSE_TYPE_XPUB_MS_REQUEST,
    DBB_RESPONSE_TYPE_CREATE_WALLET,
    DBB_RESPONSE_TYPE_INFO,
    DBB_RESPONSE_TYPE_ERASE,
    DBB_RESPONSE_TYPE_LED_BLINK,
    DBB_RESPONSE_TYPE_ADD_BACKUP,
    DBB_RESPONSE_TYPE_LIST_BACKUP,
    DBB_RESPONSE_TYPE_ERASE_BACKUP,
    DBB_RESPONSE_TYPE_RANDOM_NUM,
    DBB_RESPONSE_TYPE_DEVICE_LOCK
} dbb_response_type_t;

typedef enum DBB_PROCESS_INFOLAYER_STYLE
{
    DBB_PROCESS_INFOLAYER_STYLE_NO_INFO,
    DBB_PROCESS_INFOLAYER_STYLE_TOUCHBUTTON
} dbb_process_infolayer_style_t;

class DBBDaemonGui : public QMainWindow
{
    Q_OBJECT

public:
    explicit DBBDaemonGui(QWidget* parent = 0);
    ~DBBDaemonGui();
    void GetXPubKey();

public slots:
    //!main callback when the device gets connected/disconnected
    void changeConnectedState(bool state);
    //!slot to ask for the current session password
    void askForSessionPassword();

    void eraseClicked();
    void ledClicked();
    void setResultText(const QString& result);
    void setPasswordClicked(bool showInfo=true);
    void seed();

    //backup calls
    void showBackupDialog();
    void addBackup();
    void listBackup();
    void eraseAllBackups();
    void restoreBackup(const QString& backupFilename);

    void getRandomNumber();
    //!lock the device, disabled "backup", "verifypass" and "seed" command
    void lockDevice();

    //!enable or disable loading indication in the UI
    void setLoading(bool status);
    //!resets device infos (in case of a disconnect)
    void resetInfos();
    //!update overview flags (wallet / lock, etc)
    void updateOverviewFlags(bool walletAvailable, bool lockAvailable, bool loading);
    void JoinCopayWallet();
    void JoinCopayWalletWithXPubKey();
    void GetRequestXPubKey();
    bool checkPaymentProposals();
    void gotoOverviewPage();
    void gotoMultisigPage();
    void gotoSettingsPage();
    void getInfo();

    void parseResponse(const UniValue& response, dbb_cmd_execution_status_t status, dbb_response_type_t tag);
    void showEchoVerification(QString echoStr);
    void postSignedPaymentProposal(const UniValue& proposal, const std::vector<std::string> &vSigs);

signals:
    void showCommandResult(const QString& result);
    void deviceStateHasChanged(bool state);
    void XPubForCopayWalletIsAvailable();
    void RequestXPubKeyForCopayWalletIsAvailable();
    void gotResponse(const UniValue& response, dbb_cmd_execution_status_t status, dbb_response_type_t tag);

    void shouldVerifySigning(const QString& signature);
    void signedProposalAvailable(const UniValue& proposal, const std::vector<std::string> &vSigs);

private:
    Ui::MainWindow* ui;
    BackupDialog *backupDialog;
    QLabel* statusBarLabelLeft;
    QLabel* statusBarLabelRight;
    QPushButton* statusBarButton;
    bool processComnand;
    bool deviceConnected;
    bool cachedWalletAvailableState;
    std::string sessionPassword; //TODO: needs secure space / mem locking
    std::string sessionPasswordDuringChangeProcess; //TODO: needs secure space / mem locking
    std::vector<DBBMultisigWallet> vMultisigWallets;

    //!check device state and do a UI update
    void checkDevice();

    bool sendCommand(const std::string& cmd, const std::string& password, dbb_response_type_t tag = DBB_RESPONSE_TYPE_UNKNOWN);
    void _JoinCopayWallet();
    bool QTexecuteCommandWrapper(const std::string& cmd, const dbb_process_infolayer_style_t layerstyle, std::function<void(const std::string&, dbb_cmd_execution_status_t status)> cmdFinished);

    QAction *overviewAction;
    QAction *walletsAction;
    QAction *settingsAction;
};

#endif
