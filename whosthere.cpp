/*
 * Copyright (C) 2013 Matthias Gehre <gehre.matthias@gmail.com>
 *
 * This work is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This work is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this work; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <TelepathyQt/Debug>
#include <TelepathyQt/AccountSet>
#include <TelepathyQt/ConnectionManager>
#include <TelepathyQt/AccountManager>
#include <TelepathyQt/ContactManager>
#include <TelepathyQt/AccountFactory>
#include <TelepathyQt/PendingReady>
#include <TelepathyQt/PendingAccount>
#include <TelepathyQt/PendingStringList>
#include <TelepathyQt/ContactMessenger>

#include "whosthere.h"
#include "telepathyclient.h"

using namespace std;

WhosThere::WhosThere(QQuickItem *parent) :
    QQuickItem(parent)
{

    AccountFactoryPtr accountFactory = AccountFactory::create(QDBusConnection::sessionBus(),
        Account::FeatureCore);

    ConnectionFactoryPtr connectionFactory = ConnectionFactory::create(QDBusConnection::sessionBus(),
                Connection::FeatureConnected /*| Connection::FeatureConnected |
                Connection::FeatureRoster | Connection::FeatureRosterGroups*/);

    ChannelFactoryPtr channelFactory = ChannelFactory::create(QDBusConnection::sessionBus());

    ContactFactoryPtr contactFactory = ContactFactory::create(Contact::FeatureAlias | Contact::FeatureSimplePresence);

    mCR = ClientRegistrar::create(accountFactory, connectionFactory, channelFactory);

    mHandler = TelepathyClient::create();
    QString handlerName(QLatin1String("WhosThereGui"));
    if (!mCR->registerClient(AbstractClientPtr::dynamicCast(mHandler), handlerName)) {
        qWarning() << "Unable to register incoming file transfer handler, aborting";
    }

    mAM = Tp::AccountManager::create(accountFactory, connectionFactory, channelFactory, contactFactory);
    qDebug() << "Waiting for account manager";
    connect(mAM->becomeReady(), &PendingOperation::finished,
            this, &WhosThere::onAMReady);
    /*connect(mAM.data(),
                SIGNAL(newAccount(const Tp::AccountPtr &)),
                SLOT(onNewAccount(const Tp::AccountPtr &)));*/
}


WhosThere::~WhosThere() {

}

void WhosThere::onAMReady(Tp::PendingOperation *op)
{
    if (op->isError()) {
        qWarning() << "AM cannot become ready -" <<
            op->errorName() << ": " << op->errorMessage();
        return;
    }

    QList< AccountPtr >  accounts = mAM->accountsByProtocol("whatsapp")->accounts();
    qDebug() << "number of accounts: " << accounts.size();
    if(accounts.size() == 0) {
        qDebug() << "Creating new account";
        emit noAccount();
    } else {

        emit accountOk();
        mAccount = *accounts.begin();
        connect(mAccount->becomeReady(), &PendingOperation::finished,
                this, &WhosThere::onAccountFinished);
    }
}

void WhosThere::onAccountCreateFinished(PendingOperation* op) {
    if (op->isError()) {
        qWarning() << "Account cannot become created -" <<
            op->errorName() << ": " << op->errorMessage();
        return;
    }
    emit accountOk();
    mAccount = dynamic_cast<PendingAccount*>(op)->account();
    connect(mAccount->becomeReady(), &PendingOperation::finished,
            this, &WhosThere::onAccountFinished);
}

void WhosThere::onAccountFinished(PendingOperation* op) {
    if (op->isError()) {
        qWarning() << "Account cannot become ready -" <<
            op->errorName() << ": " << op->errorMessage();
        return;
    }
    if(mAccount.isNull()) {
        qDebug() << "hosThere::onAccountFinished: mAccount == NULL";
        return;
    }
    //qDebug() << "WhosThere::setAccount nickname: " << account->cmName();
    //qDebug() << "WhosThere::setAccount valid : " << mAccount->isValidAccount() << " enabled: " << mAccount->isEnabled();
    connect( mAccount.data(), &Account::stateChanged,
             this, &WhosThere::accountEnabledChanged);
    accountEnabledChanged(mAccount->isEnabled());

    connect( mAccount.data(), &Account::validityChanged,
             this, &WhosThere::accountValidityChanged);
    accountValidityChanged(mAccount->isValid());

    connect( mAccount.data(), &Account::invalidated,
             this, &WhosThere::onAccountInvalidated);

    connect( mAccount.data(), &Account::parametersChanged,
             this, &WhosThere::accountParametersChanged);
    accountParametersChanged( mAccount->parameters() );

    m_simpleTextObserver = SimpleTextObserver::create(mAccount);
    connect(m_simpleTextObserver.data(), &SimpleTextObserver::messageReceived,
            this, &WhosThere::onMessageReceived);

    connect(mAccount.data(), &Account::connectionChanged,
            this, &WhosThere::onAccountConnectionChanged);
    onAccountConnectionChanged(mAccount->connection());
}

void WhosThere::enableAccount(bool enabled) {
    if(!mAccount.isNull())
        connect(mAccount->setEnabled(enabled), &PendingOperation::finished,
                this, &WhosThere::onPendingOperation);
}

void WhosThere::onAccountInvalidated() {
    mAccount.reset();
    emit noAccount();
}

void WhosThere::onMessageReceived(const Tp::ReceivedMessage &message, const Tp::TextChannelPtr &channel) {
    qDebug() << "WhosThere::onMessageReceived isDeliveryReport: " << message.isDeliveryReport()
             << " text: " << message.text()
             << " sender: " << message.sender()->id();
}

void WhosThere::onPendingOperation(PendingOperation* acc) {
    if (acc->isError()) {
        qWarning() << "Pending operation failed: " <<
            acc->errorName() << ": " << acc->errorMessage();
        return;
    }
}

void WhosThere::onAccountConnectionChanged(const ConnectionPtr &conn)
{
    if (conn) {
        qDebug() << "WhosThere::onAccountConnectionChanged";
        mConn = conn;
        connect(mConn.data(), &Connection::statusChanged,
                this, &WhosThere::onConnectionStatusChanged);
        onConnectionStatusChanged( mConn->status() );
        connect(mConn->contactManager().data(), &ContactManager::stateChanged,
                this, &WhosThere::onContactManagerStateChanged);
        onContactManagerStateChanged(mConn->contactManager()->state());
    } else {
        emit connectionStatusChanged("disconnected");
        qDebug() << " WhosThere::onAccountConnectionChanged: conn = NULL";
    }
}

void WhosThere::onConnectionStatusChanged(uint status) {
    switch(status) {
    case ConnectionStatusDisconnected:
        emit connectionStatusChanged("disconnected");
        connectAccount();
        break;
    case ConnectionStatusConnecting:
        emit connectionStatusChanged("connecting");
        break;
    case ConnectionStatusConnected:
        emit connectionStatusChanged("connected");
        break;
    }
}

void WhosThere::onContactManagerStateChanged(ContactListState state)
{
    qDebug() << "WhosThere::onContactManagerStateChanged " << state;
    if (state == ContactListStateSuccess) {
        qDebug() << "Loading contacts";
        foreach (const ContactPtr &contact, mConn->contactManager()->allKnownContacts()) {
            qDebug() << "Contact id: " << contact->id();
        }
    }
}

void WhosThere::message_send(QString jid, QByteArray message)
{
    ContactMessengerPtr contactMessenger = ContactMessenger::create(mAccount, jid);
    if(!contactMessenger) {
        qDebug() << "WhosThere::message_send: contactMessenger = NULL";
        return;
    }
    contactMessenger->sendMessage(message);
}

void WhosThere::set_account(const QString& phonenumber, const QString& password)
{
    QVariantMap parameters;
    if(phonenumber.length() > 0)
        parameters.insert( "account", QVariant(phonenumber));
    if(password.length() > 0)
        parameters.insert( "password", QVariant(password));

    if(mAccount.isNull()) {
        if(phonenumber.length() == 0 || password.length() == 0) {
            qWarning() << "WhosThere::set_account: phonenumber.length() == 0 || password.length() == 0";
            return;
        }
        QVariantMap properties;
        properties.insert( "org.freedesktop.Telepathy.Account.Enabled", true );
        PendingAccount* acc = mAM->createAccount("whosthere", "whatsapp", "WhatApp Account", parameters, properties);

        connect(acc, &PendingAccount::finished,
                this, &WhosThere::onAccountCreateFinished );
    } else {
        PendingStringList* sl = mAccount->updateParameters(parameters, QStringList());
        connect(sl, &Tp::PendingStringList::finished,
                this, &WhosThere::onPendingOperation);
    }
}

void WhosThere::connectAccount() {
    if(!mAccount.isNull())
        mAccount->setRequestedPresence(Presence::available());
}

void WhosThere::removeAccount() {
    if(!mAccount.isNull())
        mAccount->remove();
}

void WhosThere::disconnect() {
    if(!mAccount.isNull())
        mAccount->setRequestedPresence(Presence::offline());
}

/*                                             Registration                                            */
void WhosThere::code_request(const QString& cc, const QString& phonenumber, const QString &uid, bool useText)
{
    if(uid.length() != 32){
        qWarning() << "WhosThere::code_request : uid.length() != 32";
        return;
    }
    WhosThere::requestCode(cc, phonenumber, uid, useText,
                           [this] (const QString& status, const QString& reason) {
                                emit code_request_response(status, reason);
                            });
}

void WhosThere::code_register(const QString& cc, const QString& phonenumber, const QString& uid, const QString& code_)
{
    if(uid.length() != 32) {
        qWarning() << "WhosThere::code_register : uid.length() != 32";
        return;
    }
    QString code = code_;
    if(code.length() == 7) //remove hyphon
        code = code.left(3) + code.right(3);
    if(code.length() != 6) {
        qWarning() << "WhosThere::code_register : code.length() != 6";
        return;
    }

    WhosThere::registerCode(cc, phonenumber, uid, code,
                           [this] (const QString& status, const QString& pw) {
                                emit code_register_response(status, pw);
                            });
}

void WhosThere::requestCode(const QString& cc, const QString& phoneNumber,
                            const QString& uid, bool useText, std::function<void(const QString&, const QString&)> callback) {
    QUrlQuery urlQuery;
    urlQuery.addQueryItem("cc", cc);
    urlQuery.addQueryItem("in", phoneNumber);
    urlQuery.addQueryItem("lc", "US");
    urlQuery.addQueryItem("lg", "en");
    urlQuery.addQueryItem("mcc", "000");
    urlQuery.addQueryItem("mnc", "000");
    urlQuery.addQueryItem("method", useText ? "sms" : "voice");
    urlQuery.addQueryItem("id", uid);
    QString token = QCryptographicHash::hash(
                (QLatin1String("PdA2DJyKoUrwLw1Bg6EIhzh502dF9noR9uFCllGk1354754753509") + phoneNumber).toLatin1(),
                                     QCryptographicHash::Md5).toHex();
    urlQuery.addQueryItem("token", token);

    QUrl url("https://v.whatsapp.net/v2/code");
    url.setQuery(urlQuery);

    QNetworkRequest request;
    request.setHeader(QNetworkRequest::UserAgentHeader, "WhatsApp/2.3.53 S40Version/14.26 Device/Nokia302");
    request.setRawHeader("Accept","text/json");
    request.setUrl(url);
    qDebug() << url;

    QNetworkAccessManager *manager = new QNetworkAccessManager();
    connect(manager, &QNetworkAccessManager::finished,
            [manager,callback] (QNetworkReply* reply) {
                    if(reply->error() != QNetworkReply::NoError) {
                        qDebug() << "Http error " << reply->error();
                        callback("fail", "http error");
                    } else {
                        QByteArray data = reply->readAll();
                        qDebug() << "Reply: " << data;
                        QJsonDocument jsonDocument = QJsonDocument::fromJson(data);
                        QJsonObject jsonObject = jsonDocument.object();
                        if(!jsonObject.contains("status")) {
                            qDebug() << "status: " << jsonObject["status"];
                            callback("fail","malformed");
                        } else {
                            QString reason;
                            if(jsonObject.contains("reason"))
                                reason = jsonObject["status"].toString();
                            callback(jsonObject["status"].toString(), reason);
                        }
                    }
                    reply->deleteLater();
                    manager->deleteLater();
                });
    manager->get(request);
}

void WhosThere::registerCode(const QString& cc, const QString& phoneNumber,
                        const QString& uid, const QString& code, std::function<void(const QString&,const QString&)> callback) {
    QUrlQuery urlQuery;
    urlQuery.addQueryItem("cc", cc);
    urlQuery.addQueryItem("in", phoneNumber);
    urlQuery.addQueryItem("id", uid);
    urlQuery.addQueryItem("code", code);
    QString token = QCryptographicHash::hash(
                (QLatin1String("PdA2DJyKoUrwLw1Bg6EIhzh502dF9noR9uFCllGk1354754753509") + phoneNumber).toLatin1(),
                                     QCryptographicHash::Md5).toHex();
    urlQuery.addQueryItem("token", token);

    QUrl url("https://v.whatsapp.net/v2/register");
    url.setQuery(urlQuery);

    QNetworkRequest request;
    request.setHeader(QNetworkRequest::UserAgentHeader, "WhatsApp/2.3.53 S40Version/14.26 Device/Nokia302");
    request.setRawHeader("Accept","text/json");
    request.setUrl(url);
    qDebug() << url;

    QNetworkAccessManager *manager = new QNetworkAccessManager();
    connect(manager, &QNetworkAccessManager::finished,
            [manager,callback] (QNetworkReply* reply) {
                    if(reply->error() != QNetworkReply::NoError) {
                        qDebug() << "Http error " << reply->error();
                        callback("http error","");
                    } else {
                        QByteArray data = reply->readAll();
                        qDebug() << "Reply: " << data;
                        QJsonDocument jsonDocument = QJsonDocument::fromJson(data);
                        QJsonObject jsonObject = jsonDocument.object();
                        if(!jsonObject.contains("status") || !jsonObject.contains("pw")) {
                            qDebug() << "status: " << jsonObject["status"];
                            callback("malformed","");
                        } else {
                            callback(jsonObject["status"].toString(),jsonObject["pw"].toString());
                        }
                    }
                    reply->deleteLater();
                    manager->deleteLater();
                });
    manager->get(request);
}
