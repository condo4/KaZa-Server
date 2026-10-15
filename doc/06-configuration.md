# 06 — Configuration

This document covers `kazad` system configuration: the `kazad.conf` file,
SSL certificates, database setup, and service management.

---

## kazad.conf

The daemon reads `/etc/kazad.conf` at startup. The file uses INI format
(Qt `QSettings::IniFormat`).

### [ssl] — SSL server

```ini
[ssl]
hostname    = myhome.example.com   ; Used in the generated certificate (SAN/CN)
keypassword = changeme             ; Password for the private key file
port        = 4242                 ; Port the app clients connect to
```

| Key | Required | Description |
|---|---|---|
| `hostname` | Yes | Hostname or IP for the SSL certificate. Must match what clients use to connect. |
| `keypassword` | Yes | Password used to encrypt the server private key. |
| `port` | No (default 4242) | TCP port for the main SSL server. |

### [control] — control port

```ini
[control]
enable = true    ; true to start the control port (default: true)
port   = 4243   ; Port for the control server (no client cert required)
```

The control port uses SSL without client certificate verification. Its primary
purpose is distributing `ca.cert.pem` to new clients that do not yet have a
trusted certificate. It can be disabled if not needed.

### [qml] — QML files

```ini
[qml]
server = /etc/kazad/main.qml          ; Server-side automation QML
client = /var/lib/kazad/app.rcc       ; Client UI resource bundle
```

| Key | Description |
|---|---|
| `qml/server` | Path to the server automation QML entry point. Loaded by the QML engine at startup. |
| `qml/client` | Path to the client UI resource bundle (`app.rcc`). Sent to clients in response to `APP?`. If empty, no client app is served. |

### [database] — PostgreSQL

```ini
[database]
driver   = QPSQL
dbName   = kazadb
hostname = localhost
port     = 5432
username = kaza
password = changeme
```

The database is used for:
- `InternalObject` persistence (read and written by the daemon itself)
- `kazamanager.runDbQuery()` calls from server QML
- `KzHistory` queries from connected clients

The database is optional. If `database/driver` is empty, the database
is not configured and `InternalObject` will lose its values on restart,
`runDbQuery` will silently fail, and `KzHistory` will return no data.

Supported Qt SQL drivers: `QPSQL` (PostgreSQL), `QMYSQL` (MySQL/MariaDB),
`QSQLITE` (SQLite — for single-machine installs without a separate DB server).

---

## SSL certificates

### Automatic generation

If any of the three certificate files are missing from `/var/lib/kazad/`, the
daemon generates a complete certificate set on startup:

```
/var/lib/kazad/ca.cert.pem      — CA certificate
/var/lib/kazad/server.cert.pem  — Server certificate (signed by CA)
/var/lib/kazad/server.key       — Server private key (encrypted)
```

The CA and server certificate are self-signed. The `ssl/hostname` value is
embedded in the certificate as the Common Name and Subject Alternative Name.

### Certificate distribution to clients

Clients must trust `ca.cert.pem` to verify the server's identity. Distribute
it by:

1. **Manual copy**: copy `ca.cert.pem` to each client device via USB, SSH, or
   secure file transfer.

2. **Control port**: the control port (`:4243`) serves the certificate without
   requiring a client certificate. A first-run client can retrieve it over the
   local network. This is convenient but requires the user to verify the
   certificate fingerprint on first trust.

3. **HTTPS**: host `ca.cert.pem` on a web server the user already trusts, and
   have clients download it from there.

### Regenerating certificates

Delete the certificate files and restart the daemon:

```bash
rm /var/lib/kazad/ca.cert.pem \
   /var/lib/kazad/server.cert.pem \
   /var/lib/kazad/server.key
systemctl restart kazad
```

After regeneration, all clients must receive the new `ca.cert.pem` and
re-trust it. Existing client sessions will be rejected until they update.

---

## Database setup

### PostgreSQL

```bash
# Create the database and user
createdb kazadb
createuser kaza
psql -c "ALTER USER kaza WITH PASSWORD 'changeme';"
psql -c "GRANT ALL PRIVILEGES ON DATABASE kazadb TO kaza;"

# Connect and create tables
psql -U kaza -d kazadb
```

Minimal schema for `InternalObject` persistence (created automatically by the
daemon if it can connect):

```sql
-- For InternalObject persistence
CREATE TABLE IF NOT EXISTS internal_objects (
    name  TEXT PRIMARY KEY,
    value TEXT
);
```

For time-series data from `runDbQuery`, create your own tables:

```sql
-- Example time-series table
CREATE TABLE IF NOT EXISTS datapoints (
    id    SERIAL PRIMARY KEY,
    name  TEXT NOT NULL,
    value DOUBLE PRECISION,
    time  TIMESTAMPTZ DEFAULT now()
);
CREATE INDEX ON datapoints (name, time DESC);

-- Example energy table (indexed by numeric object ID)
CREATE TABLE IF NOT EXISTS power (
    id    SERIAL PRIMARY KEY,
    gad   INTEGER,
    power DOUBLE PRECISION,
    time  TIMESTAMPTZ DEFAULT now()
);
```

---

## systemd service

Create `/etc/systemd/system/kazad.service`:

```ini
[Unit]
Description=KaZa Home Automation Daemon
After=network-online.target postgresql.service
Wants=network-online.target

[Service]
Type=simple
User=kaza
ExecStart=/usr/local/bin/kazad
Restart=on-failure
RestartSec=5
StandardOutput=journal
StandardError=journal

# Allow reading config and certs
ReadOnlyPaths=/etc/kazad.conf
ReadWritePaths=/var/lib/kazad

[Install]
WantedBy=multi-user.target
```

```bash
systemctl daemon-reload
systemctl enable kazad
systemctl start kazad
journalctl -u kazad -f    # follow logs
```

---

## File layout summary

```
/etc/kazad.conf                     — main configuration
/etc/kazad/main.qml                 — server automation entry point
/etc/kazad/*.qml                    — your server QML modules
/var/lib/kazad/ca.cert.pem          — CA certificate (distribute to clients)
/var/lib/kazad/server.cert.pem      — server certificate
/var/lib/kazad/server.key           — encrypted private key
/var/lib/kazad/app.rcc              — client UI bundle
/usr/lib/<arch>/qt6/qml/org/kazoe/  — installed plugin .so files
```

---

## Logging

`kazad` logs to stdout/stderr using Qt's logging framework. When run under
systemd, logs go to the journal.

Log levels used:
- `qDebug()` — verbose protocol tracing (enable `DEBUG_CONNECTION` or `DEBUG_FRAME` compile flags)
- `qInfo()` — normal operational events (connections, authentications, reloads)
- `qWarning()` — recoverable errors (bad frames, missing objects, validation failures)
- `qCritical()` — startup failures (certificate generation, missing config)

Filter by severity with:

```bash
journalctl -u kazad --priority=warning    # warnings and above only
```
