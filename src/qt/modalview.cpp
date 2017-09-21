// Copyright (c) 2015 Jonas Schnelli / Shift Devices AG
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "modalview.h"

#include <QMessageBox>

#include "dbb_gui.h"
#include "dbb_util.h"

ModalView::ModalView(QWidget* parent) : QWidget(parent), ui(new Ui::ModalView), txPointer(0)
{
    ui->setupUi(this);

    connect(this->ui->setDeviceNameOnly, SIGNAL(returnPressed()), this->ui->setDeviceSubmit, SIGNAL(clicked()));
    connect(this->ui->setDeviceName, SIGNAL(returnPressed()), this->ui->setPasswordNew, SLOT(setFocus()));
    connect(this->ui->setPasswordOld, SIGNAL(returnPressed()), this->ui->setPasswordNew, SLOT(setFocus()));
    connect(this->ui->setPasswordNew, SIGNAL(returnPressed()), this->ui->setPasswordRepeat, SLOT(setFocus()));
    connect(this->ui->setPasswordRepeat, SIGNAL(returnPressed()), this->ui->setDeviceSubmit, SIGNAL(clicked()));

    connect(this->ui->setDeviceNameOnly, SIGNAL(textChanged(const QString&)), this, SLOT(inputCheck(const QString&)));
    connect(this->ui->setDeviceName, SIGNAL(textChanged(const QString&)), this, SLOT(inputCheck(const QString&)));
    connect(this->ui->setPasswordOld, SIGNAL(textChanged(const QString&)), this, SLOT(inputCheck(const QString&)));
    connect(this->ui->setPasswordNew, SIGNAL(textChanged(const QString&)), this, SLOT(inputCheck(const QString&)));
    connect(this->ui->setPasswordRepeat, SIGNAL(textChanged(const QString&)), this, SLOT(inputCheck(const QString&)));

    connect(this->ui->setDeviceSubmit, SIGNAL(clicked()), this, SLOT(deviceSubmitProvided()));
    connect(this->ui->setDeviceCancel, SIGNAL(clicked()), this, SLOT(deviceCancelProvided()));

    connect(this->ui->okButton, SIGNAL(clicked()), this, SLOT(okButtonAction()));
    connect(this->ui->showDetailsButton, SIGNAL(clicked()), this, SLOT(detailButtonAction()));
    connect(this->ui->abortButton, SIGNAL(clicked()), this, SLOT(twoFACancelPressed()));
    connect(this->ui->continueButton, SIGNAL(clicked()), this, SLOT(continuePressed()));
    connect(this->ui->upgradeFirmware, SIGNAL(clicked()), this, SLOT(upgradeFirmware()));

    ui->qrCodeSequence->useOnDarkBackground(true);
    ui->setDeviceSubmit->setFocus();
    ui->setDeviceSubmit->setEnabled(false);
    visible = false;
}

ModalView::~ModalView()
{
    delete ui;
}

void ModalView::setText(const QString& text)
{
    ui->modalInfoLabel->setVisible(true);
    ui->modalInfoLabelLA->setVisible(true);
    ui->modalInfoLabel->setText(text);
}

void ModalView::cleanse()
{
    ui->setDeviceName->clear();
    ui->setPasswordOld->clear();
    ui->setPasswordNew->clear();
    ui->setPasswordRepeat->clear();
}

void ModalView::deviceSubmitProvided()
{
    if (!ui->setDeviceSubmit->isEnabled())
        return;

    if (ui->setDeviceNameOnly->isVisible())
        emit newDeviceNameAvailable(ui->setDeviceNameOnly->text());
    else if (ui->setDeviceName->isVisible())
        emit newDeviceNamePasswordAvailable(ui->setPasswordNew->text(), ui->setDeviceName->text());
    else
        emit newPasswordAvailable(ui->setPasswordNew->text(), ui->setPasswordOld->text());

    cleanse();
}

void ModalView::deviceCancelProvided()
{
    cleanse();
    showOrHide();
}

void ModalView::showOrHide(bool state)
{
    if (state)
        setGeometry(-this->width(), 0, this->width(), this->height());

    QPropertyAnimation* animation = new QPropertyAnimation(this, "pos");
    animation->setDuration(300);
    animation->setStartValue(this->pos());
    animation->setEndValue(QPoint((state ? 0 : -this->width()), 0));
    animation->setEasingCurve(QEasingCurve::OutQuad);
    // to slide in call
    animation->start(QAbstractAnimation::DeleteWhenStopped);

    visible = state;

    emit modalViewWillShowHide(false);
}

void ModalView::setDeviceHideAll()
{
    ui->setDeviceNameOnly->clear();
    ui->setDeviceName->clear();
    ui->setPasswordOld->clear();
    ui->setPasswordNew->clear();
    ui->setPasswordRepeat->clear();
    ui->setDeviceWarning->clear();
    ui->modalInfoLabel->clear();
    ui->modalInfoLabelLA->clear();
    ui->abortButton->setVisible(false);
    ui->continueButton->setVisible(false);
    ui->upgradeFirmware->setVisible(false);
    ui->upgradeFirmware->setVisible(false);
    ui->qrCodeSequence->setVisible(false);
    ui->showDetailsButton->setVisible(false);
    ui->infoLabel->setVisible(false);
    ui->stepsLabel->setVisible(false);
    ui->okButton->setVisible(false);
    ui->setDeviceWidget->setVisible(false);
    ui->setPasswordOld->setVisible(false);
    ui->setPasswordNew->setVisible(false);
    ui->setPasswordRepeat->setVisible(false);
    ui->uninizializedInfoLabel->setVisible(false);
    ui->setDeviceName->setVisible(false);
    ui->modalInfoLabel->setVisible(false);
    ui->modalInfoLabelLA->setVisible(false);
    ui->modalIcon->setIcon(QIcon());
    ui->setDeviceNameOnly->setVisible(false);
    ui->setDevicePasswordInfo->setVisible(false);
}

void ModalView::showSetNewWallet()
{
    setDeviceHideAll();
    showOrHide(true);
    ui->setDeviceWidget->setVisible(true);
    ui->uninizializedInfoLabel->setVisible(true);
    ui->setDeviceName->setVisible(true);
    ui->setPasswordNew->setVisible(true);
    ui->setPasswordNew->setPlaceholderText(tr("Password"));
    ui->setPasswordRepeat->setVisible(true);
    ui->setDevicePasswordInfo->setVisible(true);
    ui->setDeviceName->setFocus();
    ui->setDeviceSubmit->setEnabled(false);
    ui->setDeviceCancel->setVisible(false);
}

void ModalView::showSetPassword()
{
    setDeviceHideAll();
    showOrHide(true);
    ui->setDeviceWidget->setVisible(true);
    ui->setPasswordOld->setVisible(true);
    ui->setPasswordNew->setVisible(true);
    ui->setPasswordNew->setPlaceholderText(tr("New password"));
    ui->setPasswordRepeat->setVisible(true);
    ui->setDevicePasswordInfo->setVisible(true);
    ui->setPasswordOld->setFocus();
    ui->setDeviceSubmit->setEnabled(false);
    ui->setDeviceCancel->setVisible(true);
}

void ModalView::showSetDeviceNameCreate()
{
    setDeviceHideAll();
    showOrHide(true);
    ui->setDeviceWidget->setVisible(true);
    ui->setDeviceNameOnly->setVisible(true);
    ui->setDeviceNameOnly->setFocus();
    ui->setDeviceSubmit->setEnabled(false);
}

void ModalView::showModalInfo(const QString &info, int helpType)
{
    setDeviceHideAll();
    showOrHide(true);
    ui->modalInfoLabel->setVisible(true);
    ui->modalInfoLabelLA->setVisible(true);
    ui->modalInfoLabel->setText(info);
    QWidget* slide = this;
    // setup slide
    slide->setGeometry(-slide->width(), 0, slide->width(), slide->height());

    if (helpType == DBB_PROCESS_INFOLAYER_STYLE_TOUCHBUTTON)
    {
        QIcon newIcon;
        newIcon.addPixmap(QPixmap(":/icons/touchhelp"), QIcon::Normal);
        newIcon.addPixmap(QPixmap(":/icons/touchhelp"), QIcon::Disabled);
        ui->modalIcon->setIcon(newIcon);

        if (info.isNull() || info.size() == 0)
            ui->modalInfoLabel->setText(tr(""));
    }
    else if (helpType == DBB_PROCESS_INFOLAYER_STYLE_REPLUG)
    {
        QIcon newIcon;
        newIcon.addPixmap(QPixmap(":/icons/touchhelp_replug"), QIcon::Normal);
        newIcon.addPixmap(QPixmap(":/icons/touchhelp_replug"), QIcon::Disabled);
        ui->modalIcon->setIcon(newIcon);

        if (info.isNull() || info.size() == 0)
            ui->modalInfoLabel->setText(tr(""));
    }
    else if (helpType == DBB_PROCESS_INFOLAYER_CONFIRM_WITH_BUTTON || helpType == DBB_PROCESS_INFOLAYER_CONFIRM_WITH_BUTTON_WARNING)
    {
        if (helpType == DBB_PROCESS_INFOLAYER_CONFIRM_WITH_BUTTON_WARNING)
        {
            QIcon newIcon;
            newIcon.addPixmap(QPixmap(":/icons/modal_warning"), QIcon::Normal);
            newIcon.addPixmap(QPixmap(":/icons/modal_warning"), QIcon::Disabled);
            ui->modalIcon->setIcon(newIcon);
        }
        else
            ui->modalIcon->setIcon(QIcon());

        ui->okButton->setVisible(true);
        ui->okButton->setFocus();
    }
    else if (helpType == DBB_PROCESS_INFOLAYER_UPGRADE_FIRMWARE)
    {
        ui->upgradeFirmware->setVisible(true);
        ui->upgradeFirmware->setFocus();
    }
    else
    {
        ui->modalIcon->setIcon(QIcon());
    }


    // then a animation:
    QPropertyAnimation* animation = new QPropertyAnimation(slide, "pos");
    animation->setDuration(300);
    animation->setStartValue(slide->pos());
    animation->setEndValue(QPoint(0, 0));
    animation->setEasingCurve(QEasingCurve::OutQuad);
    // to slide in call
    animation->start(QAbstractAnimation::DeleteWhenStopped);

    emit modalViewWillShowHide(true);
}

void ModalView::showTransactionVerification(bool twoFAlocked, bool showQRSqeuence, int step, int steps)
{
    QString longString;

    longString += "Sending: ";

    UniValue amountUni = find_value(txData, "amount");
    if (amountUni.isNum())
    {
        longString += "<strong>"+QString::fromStdString(DBB::formatMoney(amountUni.get_int64()))+"</strong><br />";
    }

    UniValue toAddressUni = find_value(txData, "toAddress");
    if (!toAddressUni.isStr())
    {
        // try to get the address from the outputs
        UniValue outputs = find_value(txData, "outputs");
        if (outputs.isArray()) {
            toAddressUni = find_value(outputs[0], "toAddress");
        }
    }
    if (toAddressUni.isStr())
    {
        longString += "to <strong>"+QString::fromStdString(toAddressUni.get_str())+"</strong><br />";
    }

    UniValue feeUni = find_value(txData, "fee");
    if (feeUni.isNum())
    {
        longString += "Additional Fee: " + QString::fromStdString(DBB::formatMoney(feeUni.get_int64()));
        longString += "<br />-----------------------<br /><strong>Total: " + QString::fromStdString(DBB::formatMoney(amountUni.get_int64()+feeUni.get_int64())) + "</strong>";
    }

    showModalInfo("", DBB_PROCESS_INFOLAYER_STYLE_TOUCHBUTTON);
    ui->modalInfoLabelLA->setText(longString);
    ui->abortButton->setVisible(twoFAlocked);
    ui->qrCodeSequence->setData(txEcho);
    ui->modalInfoLabel->setVisible(true);
    ui->modalInfoLabelLA->setVisible(true);
    ui->showDetailsButton->setText(tr("Show Verification QR Codes"));

    if (steps > 1) {
        ui->showDetailsButton->setVisible(false);
        ui->infoLabel->setVisible(true);
        ui->stepsLabel->setVisible(true);
        ui->infoLabel->setText(tr("Signing large transaction. Please be patient..."));
        ui->stepsLabel->setText(tr("<strong>Touch %1 of %2</strong>").arg(QString::number(step), QString::number(steps)));
    }
    ui->modalIcon->setVisible(true);
    if (showQRSqeuence && steps == 1) {
        ui->showDetailsButton->setVisible(true);

//        ui->continueButton->setVisible(true);
//        setQrCodeVisibility(true);
//        ui->modalIcon->setVisible(false);
//        ui->showDetailsButton->setVisible(false);
    }

    if (twoFAlocked) {
        //ui->continueButton->setVisible(true);

        QIcon newIcon;
        newIcon.addPixmap(QPixmap(":/icons/touchhelp_smartverification"), QIcon::Normal);
        newIcon.addPixmap(QPixmap(":/icons/touchhelp_smartverification"), QIcon::Disabled);
        updateIcon(newIcon);
    }
}

void ModalView::detailButtonAction()
{
    setQrCodeVisibility(!ui->qrCodeSequence->isVisible());
}

void ModalView::setQrCodeVisibility(bool state)
{
    if (!state)
    {
        ui->showDetailsButton->setText(tr("Show Verification QR Codes"));
        ui->qrCodeSequence->setVisible(false);

//        QIcon newIcon;
//        newIcon.addPixmap(QPixmap(txPointer ? ":/icons/touchhelp_smartverification" : ":/icons/touchhelp"), QIcon::Normal);
//        newIcon.addPixmap(QPixmap(txPointer ? ":/icons/touchhelp_smartverification" : ":/icons/touchhelp"), QIcon::Disabled);
//        ui->modalIcon->setIcon(newIcon);
        ui->modalIcon->setVisible(true);
    }
    else
    {
        ui->showDetailsButton->setText(tr("Hide Verification Code"));
        ui->qrCodeSequence->setVisible(true);
        ui->modalIcon->setVisible(false);
    }
}

void ModalView::proceedFrom2FAToSigning(const QString &twoFACode)
{
    ui->qrCodeSequence->setVisible(false);
    ui->abortButton->setVisible(false);
    ui->continueButton->setVisible(false);

    emit signingShouldProceed(twoFACode, twoFACode.isEmpty() ? NULL : txPointer, txData, txType);
}

void ModalView::twoFACancelPressed()
{
    setQrCodeVisibility(false);
    ui->abortButton->setVisible(false);

    if (txPointer)
    {
        emit signingShouldProceed(QString(), NULL, txData, txType);
    }
}

void ModalView::okButtonAction()
{
    showOrHide();
}

void ModalView::setTXVerificationData(void *info, const UniValue& data, const std::string& echo, int type)
{
    txPointer = info;
    txData = data;
    txEcho = echo;
    txType = type;
}

void ModalView::clearTXData()
{
    txPointer = NULL;
    txData = UniValue(UniValue::VNULL);
    txEcho.clear();
    txType = 0;
}

void ModalView::updateIcon(const QIcon& icon)
{
    ui->modalIcon->setIcon(icon);
}

void ModalView::keyPressEvent(QKeyEvent* event)
{
    if ((event->key()==Qt::Key_Return) && visible && ui->okButton->isVisible())
        showOrHide(false);
}

void ModalView::inputCheck(const QString& sham)
{
    if (ui->setDeviceName->isVisible() || ui->setDeviceNameOnly->isVisible()) {
        QString name = QString();
        if (ui->setDeviceName->isVisible())
            name = ui->setDeviceName->text();
        else if (ui->setDeviceNameOnly->isVisible())
            name = ui->setDeviceNameOnly->text();

        if (name.size() < 1)
        {
            ui->setDeviceWarning->setText(tr("Enter a name"));
            ui->setDeviceSubmit->setEnabled(false);
            return;
        }
        QRegExp nameMatcher("^[0-9A-Z-_ ]{1,64}$", Qt::CaseInsensitive);
        if (!nameMatcher.exactMatch(name))
        {
            ui->setDeviceWarning->setText(tr("Name has invalid character"));
            ui->setDeviceSubmit->setEnabled(false);
            return;
        }
    }

    if (ui->setPasswordOld->isVisible()) {
        if (ui->setPasswordOld->text().size() < 1)
        {
            ui->setDeviceWarning->setText(tr("Enter the old password"));
            ui->setDeviceSubmit->setEnabled(false);
            return;
        }
    }

    if (ui->setPasswordNew->isVisible()) {
        if (ui->setPasswordNew->text().size() < 4)
        {
            if (ui->setPasswordNew->text().size() > 0)
                ui->setDeviceWarning->setText(tr("Password too short"));
            ui->setDeviceSubmit->setEnabled(false);
            return;
        }
        if (ui->setPasswordNew->text() != ui->setPasswordRepeat->text())
        {
            ui->setDeviceSubmit->setEnabled(false);
            ui->setDeviceWarning->setText(tr("Password not identical"));
            return;
        }
    }
    ui->setDeviceSubmit->setEnabled(true);
    ui->setDeviceWarning->setText("");
}

void ModalView::continuePressed()
{
    if (txPointer)
    {
        setQrCodeVisibility(false);
        ui->continueButton->setVisible(false);
        emit signingShouldProceed("", txPointer, txData, txType);
    }
}

void ModalView::upgradeFirmware()
{
    emit shouldUpgradeFirmware();
}
