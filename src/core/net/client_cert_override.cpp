/****************************************************************************
**
** Copyright (C) 2018 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtWebEngine module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "net/client_cert_override.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/task/post_task.h"
#include "base/callback_forward.h"
#include "net/ssl/client_cert_store.h"
#include "net/ssl/ssl_cert_request_info.h"
#include "net/ssl/ssl_private_key.h"
#include "net/cert/x509_certificate.h"
#include "third_party/boringssl/src/include/openssl/pem.h"
#include "third_party/boringssl/src/include/openssl/err.h"
#include "third_party/boringssl/src/include/openssl/evp.h"

#include "api/qwebengineclientcertificatestore.h"
#include "net/client_cert_store_data.h"
#include "profile_io_data_qt.h"

#include <QtNetwork/qtnetworkglobal.h>

#if defined(USE_NSS_CERTS)
#include "net/ssl/client_cert_store_nss.h"
#endif

#if defined(OS_WIN)
#include "net/ssl/client_cert_store_win.h"
#endif

#if defined(OS_MACOSX)
#include "net/ssl/client_cert_store_mac.h"
#endif

namespace {

class ClientCertIdentityOverride : public net::ClientCertIdentity
{
public:
    ClientCertIdentityOverride(
            scoped_refptr<net::X509Certificate> cert,
            scoped_refptr<net::SSLPrivateKey> key)
        : net::ClientCertIdentity(std::move(cert)), key_(std::move(key)) {}
    ~ClientCertIdentityOverride() override = default;

    void AcquirePrivateKey(
            const base::Callback<void(scoped_refptr<net::SSLPrivateKey>)> &
                    private_key_callback) override
    {
        private_key_callback.Run(key_);
    }

#if defined(OS_MACOSX)
    SecIdentityRef sec_identity_ref() const override
    {
        return nullptr;
    }
#endif

private:
    scoped_refptr<net::SSLPrivateKey> key_;
};

} // namespace

namespace QtWebEngineCore {

ClientCertOverrideStore::ClientCertOverrideStore()
    : ClientCertStore()
{
}

ClientCertOverrideStore::~ClientCertOverrideStore()
{
}

void ClientCertOverrideStore::GetClientCerts(const net::SSLCertRequestInfo &cert_request_info,
                                             const ClientCertListCallback &callback)
{
#if QT_CONFIG(ssl)
    QWebEngineClientCertificateStore *clientCertificateStore = QWebEngineClientCertificateStore::getInstance();
    const auto &clientCertOverrideData = clientCertificateStore->d_ptr->addedCerts;
    // Look for certificates in memory store
    for (int i = 0; i < clientCertOverrideData.length(); i++) {
        scoped_refptr<net::X509Certificate> cert = clientCertOverrideData[i]->certPtr;
        if (cert != NULL && cert->IsIssuedByEncoded(cert_request_info.cert_authorities)) {
            net::ClientCertIdentityList selected_identities;
            selected_identities.push_back(std::make_unique<ClientCertIdentityOverride>(cert, clientCertOverrideData[i]->keyPtr));
            callback.Run(std::move(selected_identities));
            return;
        }
    }
#endif // QT_CONFIG(ssl)

    // Continue with native cert store if matching certificate is not found in memory
    std::unique_ptr<net::ClientCertStore> store = getNativeStore();
    if (store != NULL) {
        store->GetClientCerts(cert_request_info, callback);
        return;
    }

    callback.Run(net::ClientCertIdentityList());
    return;
}

std::unique_ptr<net::ClientCertStore> ClientCertOverrideStore::getNativeStore()
{
#if defined(USE_NSS_CERTS)
    return std::unique_ptr<net::ClientCertStore>(new net::ClientCertStoreNSS(net::ClientCertStoreNSS::PasswordDelegateFactory()));
#elif defined(OS_WIN)
    return std::unique_ptr<net::ClientCertStore>(new net::ClientCertStoreWin());
#elif defined(OS_MACOSX)
    return std::unique_ptr<net::ClientCertStore>(new net::ClientCertStoreMac());
#else
    return nullptr;
#endif
}
} // namespace QtWebEngineCore
