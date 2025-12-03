#ifndef KAZACERTIFICATEGENERATOR_H
#define KAZACERTIFICATEGENERATOR_H

#include <QString>

/**
 * @brief Certificate generator using OpenSSL C API
 *
 * Generates SSL certificates for mutual TLS authentication:
 * - CA certificate (self-signed, 4096-bit RSA, 10 years)
 * - Server certificate (signed by CA, 2048-bit RSA, 10 years)
 *
 * Certificates include proper X.509v3 extensions for:
 * - Certificate Authority (CA:TRUE, keyCertSign, cRLSign)
 * - Server authentication (serverAuth, clientAuth for mutual TLS)
 * - Subject Alternative Names (DNS)
 */
class KaZaCertificateGenerator
{
public:
    /**
     * @brief Generate all required SSL certificates (CA and server)
     *
     * @param hostname Hostname for certificate CN and SAN fields
     * @param keyPassword Password to encrypt server private key
     * @param basePath Directory to store certificates (e.g., /var/lib/kazad)
     * @return true if all certificates generated successfully, false otherwise
     */
    static bool generateCertificates(const QString &hostname,
                                     const QString &keyPassword,
                                     const QString &basePath);

    /**
     * @brief Generate client certificate signed by CA
     *
     * Creates:
     * - <username>.key: 2048-bit RSA private key (unencrypted PKCS#8 for Android compatibility)
     * - <username>.csr: Certificate signing request
     * - <username>.cert.pem: Client certificate signed by CA (10 years)
     *
     * Certificate subject: CN=<username>, O=KaZa, C=FR
     * Extensions: clientAuth, digitalSignature
     *
     * Note: Keys are generated unencrypted because Android doesn't support PBES2
     * encryption (OpenSSL 3.0 default). Keys are stored in app private storage.
     *
     * @param username Username for client certificate CN
     * @param userPassword Password to encrypt client private key (currently unused)
     * @param hostname Hostname from server configuration
     * @param basePath Directory to store certificates (e.g., /var/lib/kazad)
     * @return true on success, false on failure
     */
    static bool generateClientCertificate(const QString &username,
                                          const QString &userPassword,
                                          const QString &hostname,
                                          const QString &basePath);

private:
    /**
     * @brief Generate CA private key and self-signed certificate
     *
     * Creates:
     * - ca.key: 4096-bit RSA private key (unencrypted)
     * - ca.cert.pem: Self-signed CA certificate (10 years)
     *
     * Certificate subject: CN=<hostname> CA, O=KaZa, C=FR
     * Extensions: CA:TRUE, keyCertSign, cRLSign
     *
     * @param hostname Hostname for CA certificate CN
     * @param basePath Directory to store files
     * @return true on success, false on failure
     */
    static bool generateCAKeyAndCert(const QString &hostname,
                                     const QString &basePath);

    /**
     * @brief Generate server private key and certificate signed by CA
     *
     * Creates:
     * - server.key: 2048-bit RSA private key (encrypted with DES3)
     * - server.cert.pem: Server certificate signed by CA (10 years)
     *
     * Certificate subject: CN=<hostname>, O=KaZa, C=FR
     * Extensions: serverAuth, clientAuth, SAN=DNS:<hostname>
     *
     * @param hostname Hostname for server certificate CN and SAN
     * @param keyPassword Password to encrypt server private key
     * @param basePath Directory to store files
     * @return true on success, false on failure
     */
    static bool generateServerKeyAndCert(const QString &hostname,
                                         const QString &keyPassword,
                                         const QString &basePath);

    /**
     * @brief Get last OpenSSL error message
     * @return Human-readable error string
     */
    static QString getLastOpenSSLError();
};

#endif // KAZACERTIFICATEGENERATOR_H
