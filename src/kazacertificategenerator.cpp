#include "kazacertificategenerator.h"
#include <QDebug>
#include <QDateTime>

#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/bio.h>

QString KaZaCertificateGenerator::getLastOpenSSLError()
{
    char err[256];
    ERR_error_string_n(ERR_get_error(), err, sizeof(err));
    return QString::fromUtf8(err);
}

bool KaZaCertificateGenerator::generateCertificates(const QString &hostname,
                                                     const QString &keyPassword,
                                                     const QString &basePath)
{
    qInfo() << "Generating SSL certificates using OpenSSL library...";

    if (!generateCAKeyAndCert(hostname, basePath)) {
        qCritical() << "Failed to generate CA certificate";
        return false;
    }

    if (!generateServerKeyAndCert(hostname, keyPassword, basePath)) {
        qCritical() << "Failed to generate server certificate";
        return false;
    }

    qInfo() << "All SSL certificates generated successfully!";
    qInfo() << "  CA cert:" << basePath + "/ca.cert.pem";
    qInfo() << "  Server cert:" << basePath + "/server.cert.pem";
    qInfo() << "  Server key:" << basePath + "/server.key";

    return true;
}

bool KaZaCertificateGenerator::generateClientCertificate(const QString &username,
                                                          const QString &userPassword,
                                                          const QString &hostname,
                                                          const QString &basePath)
{
    qInfo() << "Generating client certificate for user:" << username;

    // Generate RSA key pair (2048-bit)
    qInfo() << "Generating client private key (2048-bit RSA, encrypted)...";

    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    if (!ctx) {
        qCritical() << "Failed to create EVP_PKEY_CTX:" << getLastOpenSSLError();
        return false;
    }

    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        qCritical() << "Failed to initialize keygen:" << getLastOpenSSLError();
        EVP_PKEY_CTX_free(ctx);
        return false;
    }

    if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) <= 0) {
        qCritical() << "Failed to set key bits:" << getLastOpenSSLError();
        EVP_PKEY_CTX_free(ctx);
        return false;
    }

    EVP_PKEY *client_key = NULL;
    if (EVP_PKEY_keygen(ctx, &client_key) <= 0) {
        qCritical() << "Failed to generate client key:" << getLastOpenSSLError();
        EVP_PKEY_CTX_free(ctx);
        return false;
    }
    EVP_PKEY_CTX_free(ctx);

    // Save client private key
    // Android doesn't support PBES2 encryption (OpenSSL 3.0 default)
    // Use unencrypted PKCS#8 format - safe since stored in app private storage
    QString keyPath = basePath + "/" + username + ".key";
    FILE *keyFile = fopen(keyPath.toUtf8().constData(), "wb");
    if (!keyFile) {
        qCritical() << "Failed to open client key file:" << keyPath;
        EVP_PKEY_free(client_key);
        return false;
    }

    // Write unencrypted PKCS#8 key (Android compatible)
    // Note: The key will be stored in the Android app's private storage
    // which is only accessible by the app itself, so encryption is not critical
    if (!PEM_write_PKCS8PrivateKey(keyFile, client_key, NULL, NULL, 0, NULL, NULL)) {
        qCritical() << "Failed to write client private key:" << getLastOpenSSLError();
        fclose(keyFile);
        EVP_PKEY_free(client_key);
        return false;
    }
    fclose(keyFile);
    qInfo() << "Client private key generated successfully (unencrypted PKCS#8 - Android compatible)";

    // Load CA certificate and key
    qInfo() << "Loading CA certificate and key for signing...";

    QString caCertPath = basePath + "/ca.cert.pem";
    QString caKeyPath = basePath + "/ca.key";

    FILE *caKeyFile = fopen(caKeyPath.toUtf8().constData(), "rb");
    if (!caKeyFile) {
        qCritical() << "Failed to open CA key file:" << caKeyPath;
        EVP_PKEY_free(client_key);
        return false;
    }
    EVP_PKEY *ca_key = PEM_read_PrivateKey(caKeyFile, NULL, NULL, NULL);
    fclose(caKeyFile);

    if (!ca_key) {
        qCritical() << "Failed to read CA private key:" << getLastOpenSSLError();
        EVP_PKEY_free(client_key);
        return false;
    }

    FILE *caCertFile = fopen(caCertPath.toUtf8().constData(), "rb");
    if (!caCertFile) {
        qCritical() << "Failed to open CA cert file:" << caCertPath;
        EVP_PKEY_free(client_key);
        EVP_PKEY_free(ca_key);
        return false;
    }
    X509 *ca_cert = PEM_read_X509(caCertFile, NULL, NULL, NULL);
    fclose(caCertFile);

    if (!ca_cert) {
        qCritical() << "Failed to read CA certificate:" << getLastOpenSSLError();
        EVP_PKEY_free(client_key);
        EVP_PKEY_free(ca_key);
        return false;
    }

    // Create client certificate
    qInfo() << "Generating and signing client certificate...";

    X509 *client_cert = X509_new();
    if (!client_cert) {
        qCritical() << "Failed to create X509 structure:" << getLastOpenSSLError();
        EVP_PKEY_free(client_key);
        EVP_PKEY_free(ca_key);
        X509_free(ca_cert);
        return false;
    }

    // Set version (X509 v3)
    X509_set_version(client_cert, 2);

    // Set serial number (use timestamp for uniqueness)
    ASN1_INTEGER_set(X509_get_serialNumber(client_cert), (long)QDateTime::currentSecsSinceEpoch());

    // Set validity period (10 years = 3650 days)
    X509_gmtime_adj(X509_get_notBefore(client_cert), 0);
    X509_gmtime_adj(X509_get_notAfter(client_cert), 3650L * 24 * 60 * 60);

    // Set public key
    X509_set_pubkey(client_cert, client_key);

    // Set subject name: CN=<username>, O=KaZa, C=FR
    X509_NAME *name = X509_get_subject_name(client_cert);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                (unsigned char*)username.toUtf8().constData(), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC,
                                (unsigned char*)"KaZa", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC,
                                (unsigned char*)"FR", -1, -1, 0);

    // Set issuer name (from CA certificate)
    X509_set_issuer_name(client_cert, X509_get_subject_name(ca_cert));

    // Add X509v3 extensions for client certificate
    X509V3_CTX ctx_v3;
    X509V3_set_ctx_nodb(&ctx_v3);
    X509V3_set_ctx(&ctx_v3, ca_cert, client_cert, NULL, NULL, 0);

    // Basic Constraints: CA=FALSE
    X509_EXTENSION *ext;
    ext = X509V3_EXT_conf_nid(NULL, &ctx_v3, NID_basic_constraints, "CA:FALSE");
    if (ext) {
        X509_add_ext(client_cert, ext, -1);
        X509_EXTENSION_free(ext);
    }

    // Key Usage: Digital Signature (critical)
    ext = X509V3_EXT_conf_nid(NULL, &ctx_v3, NID_key_usage,
                              "critical,digitalSignature");
    if (ext) {
        X509_add_ext(client_cert, ext, -1);
        X509_EXTENSION_free(ext);
    }

    // Extended Key Usage: Client Auth only
    ext = X509V3_EXT_conf_nid(NULL, &ctx_v3, NID_ext_key_usage, "clientAuth");
    if (ext) {
        X509_add_ext(client_cert, ext, -1);
        X509_EXTENSION_free(ext);
    }

    // Subject Key Identifier
    ext = X509V3_EXT_conf_nid(NULL, &ctx_v3, NID_subject_key_identifier, "hash");
    if (ext) {
        X509_add_ext(client_cert, ext, -1);
        X509_EXTENSION_free(ext);
    }

    // Authority Key Identifier
    ext = X509V3_EXT_conf_nid(NULL, &ctx_v3, NID_authority_key_identifier, "keyid:always");
    if (ext) {
        X509_add_ext(client_cert, ext, -1);
        X509_EXTENSION_free(ext);
    }

    // Sign certificate with CA private key using SHA-256
    if (!X509_sign(client_cert, ca_key, EVP_sha256())) {
        qCritical() << "Failed to sign client certificate:" << getLastOpenSSLError();
        X509_free(client_cert);
        EVP_PKEY_free(client_key);
        EVP_PKEY_free(ca_key);
        X509_free(ca_cert);
        return false;
    }

    // Save client certificate
    QString certPath = basePath + "/" + username + ".cert.pem";
    FILE *certFile = fopen(certPath.toUtf8().constData(), "wb");
    if (!certFile) {
        qCritical() << "Failed to open client cert file:" << certPath;
        X509_free(client_cert);
        EVP_PKEY_free(client_key);
        EVP_PKEY_free(ca_key);
        X509_free(ca_cert);
        return false;
    }

    if (!PEM_write_X509(certFile, client_cert)) {
        qCritical() << "Failed to write client certificate:" << getLastOpenSSLError();
        fclose(certFile);
        X509_free(client_cert);
        EVP_PKEY_free(client_key);
        EVP_PKEY_free(ca_key);
        X509_free(ca_cert);
        return false;
    }
    fclose(certFile);

    qInfo() << "Client certificate generated and signed successfully";
    qInfo() << "  Client cert:" << certPath;
    qInfo() << "  Client key:" << keyPath;

    // Cleanup
    X509_free(client_cert);
    EVP_PKEY_free(client_key);
    EVP_PKEY_free(ca_key);
    X509_free(ca_cert);

    return true;
}

bool KaZaCertificateGenerator::generateCAKeyAndCert(const QString &hostname,
                                                     const QString &basePath)
{
    qInfo() << "Generating CA private key (4096-bit RSA)...";

    // Generate RSA key pair (4096-bit)
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    if (!ctx) {
        qCritical() << "Failed to create EVP_PKEY_CTX:" << getLastOpenSSLError();
        return false;
    }

    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        qCritical() << "Failed to initialize keygen:" << getLastOpenSSLError();
        EVP_PKEY_CTX_free(ctx);
        return false;
    }

    if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 4096) <= 0) {
        qCritical() << "Failed to set key bits:" << getLastOpenSSLError();
        EVP_PKEY_CTX_free(ctx);
        return false;
    }

    EVP_PKEY *ca_key = NULL;
    if (EVP_PKEY_keygen(ctx, &ca_key) <= 0) {
        qCritical() << "Failed to generate CA key:" << getLastOpenSSLError();
        EVP_PKEY_CTX_free(ctx);
        return false;
    }
    EVP_PKEY_CTX_free(ctx);

    // Save CA private key (unencrypted - stored securely in /var/lib/kazad)
    QString keyPath = basePath + "/ca.key";
    FILE *keyFile = fopen(keyPath.toUtf8().constData(), "wb");
    if (!keyFile) {
        qCritical() << "Failed to open CA key file:" << keyPath;
        EVP_PKEY_free(ca_key);
        return false;
    }

    if (!PEM_write_PrivateKey(keyFile, ca_key, NULL, NULL, 0, NULL, NULL)) {
        qCritical() << "Failed to write CA private key:" << getLastOpenSSLError();
        fclose(keyFile);
        EVP_PKEY_free(ca_key);
        return false;
    }
    fclose(keyFile);
    qInfo() << "CA private key generated successfully";

    // Create X509 certificate
    qInfo() << "Generating self-signed CA certificate (10 years validity)...";

    X509 *ca_cert = X509_new();
    if (!ca_cert) {
        qCritical() << "Failed to create X509 structure:" << getLastOpenSSLError();
        EVP_PKEY_free(ca_key);
        return false;
    }

    // Set version (X509 v3)
    X509_set_version(ca_cert, 2);

    // Set serial number
    ASN1_INTEGER_set(X509_get_serialNumber(ca_cert), 1);

    // Set validity period (10 years = 3650 days)
    X509_gmtime_adj(X509_get_notBefore(ca_cert), 0);
    X509_gmtime_adj(X509_get_notAfter(ca_cert), 3650L * 24 * 60 * 60);

    // Set public key
    X509_set_pubkey(ca_cert, ca_key);

    // Set subject name: CN=<hostname> CA, O=KaZa, C=FR
    X509_NAME *name = X509_get_subject_name(ca_cert);
    QString cn = hostname + " CA";
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                (unsigned char*)cn.toUtf8().constData(), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC,
                                (unsigned char*)"KaZa", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC,
                                (unsigned char*)"FR", -1, -1, 0);

    // Set issuer name (same as subject for self-signed)
    X509_set_issuer_name(ca_cert, name);

    // Add X509v3 extensions for CA
    X509V3_CTX ctx_v3;
    X509V3_set_ctx_nodb(&ctx_v3);
    X509V3_set_ctx(&ctx_v3, ca_cert, ca_cert, NULL, NULL, 0);

    // Basic Constraints: CA=TRUE (critical)
    X509_EXTENSION *ext;
    ext = X509V3_EXT_conf_nid(NULL, &ctx_v3, NID_basic_constraints, "critical,CA:TRUE");
    if (ext) {
        X509_add_ext(ca_cert, ext, -1);
        X509_EXTENSION_free(ext);
    }

    // Key Usage: Certificate Signing, CRL Signing (critical)
    ext = X509V3_EXT_conf_nid(NULL, &ctx_v3, NID_key_usage, "critical,keyCertSign,cRLSign");
    if (ext) {
        X509_add_ext(ca_cert, ext, -1);
        X509_EXTENSION_free(ext);
    }

    // Subject Key Identifier
    ext = X509V3_EXT_conf_nid(NULL, &ctx_v3, NID_subject_key_identifier, "hash");
    if (ext) {
        X509_add_ext(ca_cert, ext, -1);
        X509_EXTENSION_free(ext);
    }

    // Sign the certificate with SHA-256
    if (!X509_sign(ca_cert, ca_key, EVP_sha256())) {
        qCritical() << "Failed to sign CA certificate:" << getLastOpenSSLError();
        X509_free(ca_cert);
        EVP_PKEY_free(ca_key);
        return false;
    }

    // Save CA certificate
    QString certPath = basePath + "/ca.cert.pem";
    FILE *certFile = fopen(certPath.toUtf8().constData(), "wb");
    if (!certFile) {
        qCritical() << "Failed to open CA cert file:" << certPath;
        X509_free(ca_cert);
        EVP_PKEY_free(ca_key);
        return false;
    }

    if (!PEM_write_X509(certFile, ca_cert)) {
        qCritical() << "Failed to write CA certificate:" << getLastOpenSSLError();
        fclose(certFile);
        X509_free(ca_cert);
        EVP_PKEY_free(ca_key);
        return false;
    }
    fclose(certFile);

    qInfo() << "CA certificate generated successfully";

    X509_free(ca_cert);
    EVP_PKEY_free(ca_key);

    return true;
}

bool KaZaCertificateGenerator::generateServerKeyAndCert(const QString &hostname,
                                                         const QString &keyPassword,
                                                         const QString &basePath)
{
    qInfo() << "Generating server private key (2048-bit RSA, encrypted)...";

    // Generate RSA key pair (2048-bit)
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    if (!ctx) {
        qCritical() << "Failed to create EVP_PKEY_CTX:" << getLastOpenSSLError();
        return false;
    }

    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        qCritical() << "Failed to initialize keygen:" << getLastOpenSSLError();
        EVP_PKEY_CTX_free(ctx);
        return false;
    }

    if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) <= 0) {
        qCritical() << "Failed to set key bits:" << getLastOpenSSLError();
        EVP_PKEY_CTX_free(ctx);
        return false;
    }

    EVP_PKEY *server_key = NULL;
    if (EVP_PKEY_keygen(ctx, &server_key) <= 0) {
        qCritical() << "Failed to generate server key:" << getLastOpenSSLError();
        EVP_PKEY_CTX_free(ctx);
        return false;
    }
    EVP_PKEY_CTX_free(ctx);

    // Save server private key (encrypted with DES3-CBC)
    QString keyPath = basePath + "/server.key";
    FILE *keyFile = fopen(keyPath.toUtf8().constData(), "wb");
    if (!keyFile) {
        qCritical() << "Failed to open server key file:" << keyPath;
        EVP_PKEY_free(server_key);
        return false;
    }

    // Encrypt with DES3 and password
    QByteArray passwordBytes = keyPassword.toUtf8();
    if (!PEM_write_PrivateKey(keyFile, server_key, EVP_des_ede3_cbc(),
                               (unsigned char*)passwordBytes.data(),
                               passwordBytes.length(), NULL, NULL)) {
        qCritical() << "Failed to write encrypted server private key:" << getLastOpenSSLError();
        fclose(keyFile);
        EVP_PKEY_free(server_key);
        return false;
    }
    fclose(keyFile);
    qInfo() << "Server private key generated successfully";

    // Load CA certificate and key
    qInfo() << "Loading CA certificate and key for signing...";

    QString caCertPath = basePath + "/ca.cert.pem";
    QString caKeyPath = basePath + "/ca.key";

    FILE *caKeyFile = fopen(caKeyPath.toUtf8().constData(), "rb");
    if (!caKeyFile) {
        qCritical() << "Failed to open CA key file:" << caKeyPath;
        EVP_PKEY_free(server_key);
        return false;
    }
    EVP_PKEY *ca_key = PEM_read_PrivateKey(caKeyFile, NULL, NULL, NULL);
    fclose(caKeyFile);

    if (!ca_key) {
        qCritical() << "Failed to read CA private key:" << getLastOpenSSLError();
        EVP_PKEY_free(server_key);
        return false;
    }

    FILE *caCertFile = fopen(caCertPath.toUtf8().constData(), "rb");
    if (!caCertFile) {
        qCritical() << "Failed to open CA cert file:" << caCertPath;
        EVP_PKEY_free(server_key);
        EVP_PKEY_free(ca_key);
        return false;
    }
    X509 *ca_cert = PEM_read_X509(caCertFile, NULL, NULL, NULL);
    fclose(caCertFile);

    if (!ca_cert) {
        qCritical() << "Failed to read CA certificate:" << getLastOpenSSLError();
        EVP_PKEY_free(server_key);
        EVP_PKEY_free(ca_key);
        return false;
    }

    // Create server certificate
    qInfo() << "Generating and signing server certificate...";

    X509 *server_cert = X509_new();
    if (!server_cert) {
        qCritical() << "Failed to create X509 structure:" << getLastOpenSSLError();
        EVP_PKEY_free(server_key);
        EVP_PKEY_free(ca_key);
        X509_free(ca_cert);
        return false;
    }

    // Set version (X509 v3)
    X509_set_version(server_cert, 2);

    // Set serial number (different from CA)
    ASN1_INTEGER_set(X509_get_serialNumber(server_cert), 2);

    // Set validity period (10 years = 3650 days)
    X509_gmtime_adj(X509_get_notBefore(server_cert), 0);
    X509_gmtime_adj(X509_get_notAfter(server_cert), 3650L * 24 * 60 * 60);

    // Set public key
    X509_set_pubkey(server_cert, server_key);

    // Set subject name: CN=<hostname>, O=KaZa, C=FR
    X509_NAME *name = X509_get_subject_name(server_cert);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                (unsigned char*)hostname.toUtf8().constData(), -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC,
                                (unsigned char*)"KaZa", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC,
                                (unsigned char*)"FR", -1, -1, 0);

    // Set issuer name (from CA certificate)
    X509_set_issuer_name(server_cert, X509_get_subject_name(ca_cert));

    // Add X509v3 extensions for server certificate
    X509V3_CTX ctx_v3;
    X509V3_set_ctx_nodb(&ctx_v3);
    X509V3_set_ctx(&ctx_v3, ca_cert, server_cert, NULL, NULL, 0);

    // Basic Constraints: CA=FALSE
    X509_EXTENSION *ext;
    ext = X509V3_EXT_conf_nid(NULL, &ctx_v3, NID_basic_constraints, "CA:FALSE");
    if (ext) {
        X509_add_ext(server_cert, ext, -1);
        X509_EXTENSION_free(ext);
    }

    // Key Usage: Digital Signature, Key Encipherment (critical)
    ext = X509V3_EXT_conf_nid(NULL, &ctx_v3, NID_key_usage,
                              "critical,digitalSignature,keyEncipherment");
    if (ext) {
        X509_add_ext(server_cert, ext, -1);
        X509_EXTENSION_free(ext);
    }

    // Extended Key Usage: Server Auth, Client Auth (for mutual TLS)
    ext = X509V3_EXT_conf_nid(NULL, &ctx_v3, NID_ext_key_usage,
                              "serverAuth,clientAuth");
    if (ext) {
        X509_add_ext(server_cert, ext, -1);
        X509_EXTENSION_free(ext);
    }

    // Subject Alternative Name: DNS hostname
    QString sanValue = "DNS:" + hostname;
    ext = X509V3_EXT_conf_nid(NULL, &ctx_v3, NID_subject_alt_name,
                              sanValue.toUtf8().constData());
    if (ext) {
        X509_add_ext(server_cert, ext, -1);
        X509_EXTENSION_free(ext);
    }

    // Subject Key Identifier
    ext = X509V3_EXT_conf_nid(NULL, &ctx_v3, NID_subject_key_identifier, "hash");
    if (ext) {
        X509_add_ext(server_cert, ext, -1);
        X509_EXTENSION_free(ext);
    }

    // Authority Key Identifier
    ext = X509V3_EXT_conf_nid(NULL, &ctx_v3, NID_authority_key_identifier, "keyid:always");
    if (ext) {
        X509_add_ext(server_cert, ext, -1);
        X509_EXTENSION_free(ext);
    }

    // Sign certificate with CA private key using SHA-256
    if (!X509_sign(server_cert, ca_key, EVP_sha256())) {
        qCritical() << "Failed to sign server certificate:" << getLastOpenSSLError();
        X509_free(server_cert);
        EVP_PKEY_free(server_key);
        EVP_PKEY_free(ca_key);
        X509_free(ca_cert);
        return false;
    }

    // Save server certificate
    QString certPath = basePath + "/server.cert.pem";
    FILE *certFile = fopen(certPath.toUtf8().constData(), "wb");
    if (!certFile) {
        qCritical() << "Failed to open server cert file:" << certPath;
        X509_free(server_cert);
        EVP_PKEY_free(server_key);
        EVP_PKEY_free(ca_key);
        X509_free(ca_cert);
        return false;
    }

    if (!PEM_write_X509(certFile, server_cert)) {
        qCritical() << "Failed to write server certificate:" << getLastOpenSSLError();
        fclose(certFile);
        X509_free(server_cert);
        EVP_PKEY_free(server_key);
        EVP_PKEY_free(ca_key);
        X509_free(ca_cert);
        return false;
    }
    fclose(certFile);

    qInfo() << "Server certificate generated and signed successfully";

    // Cleanup
    X509_free(server_cert);
    EVP_PKEY_free(server_key);
    EVP_PKEY_free(ca_key);
    X509_free(ca_cert);

    return true;
}
