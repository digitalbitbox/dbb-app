// Copyright (c) 2015 Jonas Schnelli / Shift Devices AG
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "paymentproposal.h"
#include "ui/ui_paymentproposal.h"

#include <QStringListModel>
#include <QStringList>

#include "dbb_util.h"

PaymentProposal::PaymentProposal(QWidget *parent) :
QWidget(parent),
ui(new Ui::PaymentProposal)
{
    ui->setupUi(this);

    ui->bgWidget->setStyleSheet("background-color: rgba(0,0,0,15); border: 1px solid gray;");
    ui->amountLabelKey->setStyleSheet("font-weight: bold;");
    ui->feeLabelKey->setStyleSheet("font-weight: bold;");
    ui->toLabelKey->setStyleSheet("font-weight: bold;");

    connect(this->ui->acceptButton, SIGNAL(clicked()), this, SLOT(acceptPressed()));
    connect(this->ui->rejectButton, SIGNAL(clicked()), this, SLOT(rejectPressed()));

    connect(this->ui->arrowLeft, SIGNAL(clicked()), this, SLOT(prevPressed()));
    connect(this->ui->arrowRight, SIGNAL(clicked()), this, SLOT(nextPressed()));
}


void PaymentProposal::SetData(DBBWallet *walletIn, const std::string copayerID, const UniValue &pendingTxpIn, const UniValue &proposalDataIn, const std::string &prevID, const std::string &nextID)
{
    wallet = walletIn;
    pendingTxp = pendingTxpIn;
    proposalData = proposalDataIn;
    prevProposalID = prevID;
    nextProposalID = nextID;

    UniValue toAddressUni;
    toAddressUni = find_value(proposalData, "toAddress");
    if (!toAddressUni.isStr()) {
        toAddressUni = find_value(proposalData["outputs"][0], "toAddress");
        proposalData.pushKV("toAddress", toAddressUni);
    }
    if (toAddressUni.isStr())
        this->ui->toLabel->setText(QString::fromStdString(toAddressUni.get_str()));

    UniValue amountUni = find_value(proposalData, "amount");
    if (amountUni.isNum())
        this->ui->amountLabel->setText(QString::fromStdString(DBB::formatMoney(amountUni.get_int64())));

    UniValue feeUni = find_value(proposalData, "fee");
    if (feeUni.isNum())
        this->ui->feeLabel->setText(QString::fromStdString(DBB::formatMoney(feeUni.get_int64())));

    UniValue actions = find_value(proposalData, "actions");

    this->ui->arrowLeft->setVisible((prevProposalID.size() > 0));
    this->ui->arrowRight->setVisible((nextProposalID.size() > 0));


    bool skipProposal = false;
    this->ui->actionLabel->setVisible(false);
    this->ui->rejectButton->setVisible(true);
    this->ui->acceptButton->setVisible(true);

    if (actions.isArray())
    {
        for (const UniValue &oneAction : actions.getValues())
        {
            UniValue copayerIDUniValie = find_value(oneAction, "copayerId");
            UniValue actionType = find_value(oneAction, "type");

            if (!copayerIDUniValie.isStr() || !actionType.isStr())
                continue;

            if (copayerID == copayerIDUniValie.get_str() && actionType.get_str() == "accept") {
                this->ui->actionLabel->setVisible(true);
                this->ui->actionLabel->setText(tr("You have accepted this proposal."));

                this->ui->rejectButton->setVisible(false);
                this->ui->acceptButton->setVisible(false);
            }
            else if (copayerID == copayerIDUniValie.get_str() && actionType.get_str() == "reject") {
                this->ui->actionLabel->setVisible(true);
                this->ui->actionLabel->setText(tr("You have rejected this proposal."));

                this->ui->rejectButton->setVisible(false);
                this->ui->acceptButton->setVisible(false);
            }

        }
    }
}

void PaymentProposal::prevPressed()
{
    emit shouldDisplayProposal(pendingTxp, prevProposalID);
}

void PaymentProposal::nextPressed()
{
    emit shouldDisplayProposal(pendingTxp, nextProposalID);
}

void PaymentProposal::acceptPressed()
{
    emit processProposal(wallet, "", proposalData, ProposalActionTypeAccept);
}

void PaymentProposal::rejectPressed()
{
    emit processProposal(wallet, "", proposalData, ProposalActionTypeReject);
}

PaymentProposal::~PaymentProposal()
{
    delete ui;
}
