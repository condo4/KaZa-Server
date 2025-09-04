#!/bin/sh

if [ "$#" -ne 3 ]
then
    echo "./make_certificate.sh <server url> <KEY_PASSWORD_CLIENT>  <KEY_PASSWORD_SERVER>"
    exit
fi

export SERVER_URL=$1
export KEY_PASSWORD_CLIENT=$2
export KEY_PASSWORD_SERVER=$3

mkdir ssl
openssl genrsa -out ssl/ca.key 4096
openssl req -x509 -new -nodes -key ssl/ca.key -sha256 -days 3650 -out ssl/ca.cert.pem -subj "/CN=My Root CA/O=KaZa/C=FR"

openssl genrsa -des3 -passout env:KEY_PASSWORD_SERVER -out ssl/server.key 2048
openssl req -new -key ssl/server.key -passin env:KEY_PASSWORD_SERVER -out ssl/server.csr -subj "/CN=$SERVER_URL/O=KaZa/C=FR"

echo "subjectAltName=DNS:$SERVER_URL,DNS:localhost,IP:127.0.0.1" > ssl/server_ext.cnf
openssl x509 -req -in ssl/server.csr -CA ssl/ca.cert.pem -CAkey ssl/ca.key -CAcreateserial -out ssl/server.cert -days 825 -sha256 -extfile ssl/server_ext.cnf

openssl genrsa -des3 -passout env:KEY_PASSWORD_CLIENT -out ssl/client.key 2048
openssl req -new -key ssl/client.key -passin env:KEY_PASSWORD_CLIENT -out ssl/client.csr -subj "/CN=client/O=KaZa/C=FR"

openssl x509 -req -in ssl/client.csr -CA ssl/ca.cert.pem -CAkey ssl/ca.key -CAcreateserial -out ssl/client.cert -days 825 -sha256
