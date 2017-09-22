// Copyright (c) 2015 Jonas Schnelli / Shift Devices AG
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "backupdialog.h"
#include "ui/ui_backupdialog.h"

#include <QStringListModel>
#include <QStringList>

BackupDialog::BackupDialog(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::BackupDialog)
{
    ui->setupUi(this);

    //connect buttons to slots
    connect(ui->addButton, SIGNAL(clicked()), this, SLOT(addBackupPressed()));
    connect(ui->eraseAllButton, SIGNAL(clicked()), this, SLOT(eraseAllBackupPressed()));
    connect(ui->eraseSelected, SIGNAL(clicked()), this, SLOT(eraseSingleBackupPressed()));
    connect(ui->restoreButton, SIGNAL(clicked()), this, SLOT(restoreBackupPressed()));
    connect(ui->verifyButton, SIGNAL(clicked()), this, SLOT(verifyBackupPressed()));
}

void BackupDialog::showLoading(bool creatingBackup)
{
    //simple loading through list updating
    QList<QString> loadingList; loadingList << ((creatingBackup) ? tr("creating backup...") : tr("loading..."));
    ui->listView->setModel(new QStringListModel(loadingList));
    loadingState = true;
}

void BackupDialog::showList(const std::vector<std::string>& elements)
{
    QVector<QString> stringVector;
    for (const std::string& aItem : elements) {
        stringVector.push_back(QString::fromStdString(aItem).trimmed());
    }
    ui->listView->setModel(new QStringListModel(QList<QString>::fromVector(stringVector)));
    loadingState = false;
}

void BackupDialog::addBackupPressed()
{
    showLoading(true);
    emit addBackup();
}

void BackupDialog::restoreBackupPressed()
{
    if (loadingState)
        return;

    QModelIndex index = ui->listView->currentIndex();
    emit restoreFromBackup(index.data().toString());
}

void BackupDialog::eraseSingleBackupPressed()
{
    if (loadingState)
        return;

    QModelIndex index = ui->listView->currentIndex();
    emit eraseBackup(index.data().toString());
}

void BackupDialog::eraseAllBackupPressed()
{
    emit eraseAllBackups();
}

void BackupDialog::verifyBackupPressed()
{
    if (loadingState)
        return;

    QModelIndex index = ui->listView->currentIndex();
    emit verifyBackup(index.data().toString());
}

BackupDialog::~BackupDialog()
{
    delete ui;
}
