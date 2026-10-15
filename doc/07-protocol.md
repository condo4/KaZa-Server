# 07 — Binary Protocol

This document specifies the binary protocol used between `kazad` and clients
(KaZa-Client, `kls`, or any third-party implementation).

Transport: **SSL/TLS over TCP**. Both the main port (`:4242`) and the control
port (`:4243`) use the same frame format.

Protocol version: **1.0** (`PROTOCOL_VERSION_MAJOR = 1`, `PROTOCOL_VERSION_MINOR = 0`).

---

## Frame format

Every message is a frame:

```
 0        1        2        3        4        5 …
 +--------+--------+--------+--------+--------+-- … --+
 │  TYPE  │       LENGTH (4 bytes, big-endian) │ PAYLOAD│
 +--------+--------+--------+--------+--------+-- … --+
```

| Field | Size | Description |
|---|---|---|
| TYPE | 1 byte | Frame type identifier (see table below) |
| LENGTH | 4 bytes, big-endian | Byte count of the payload |
| PAYLOAD | LENGTH bytes | Frame-type-specific content |

Minimum frame size: 5 bytes (type + length, zero-length payload).

---

## Frame types

| ID (decimal) | Name | Direction | Description |
|---|---|---|---|
| 0 | `FRAME_SYSTEM` | both | Text command (colon-separated string) |
| 1 | `FRAME_FILE` | server → client | File transfer |
| 2 | `FRAME_OBJVALUE` | both | Single object value update |
| 3 | `FRAME_DBQUERY` | client → server | SQL query |
| 4 | `FRAME_DBRESULT` | server → client | SQL result |
| 5 | `FRAME_SOCKET_CONNECT` | client → server | Open proxied TCP socket |
| 6 | `FRAME_SOCKET_DATA` | both | Data on proxied TCP socket |
| 7 | `FRAME_SOCKET_STATE` | server → client | Proxied socket state |
| 8 | `FRAME_OBJLIST` | server → client | Full compressed object list |
| 9 | `FRAME_HISTORY` | client → server | Generic min/avg/max history request |
| 10 | `FRAME_HISTORYRESULT` | server → client | Generic min/avg/max history result |
| 255 | `FRAME_VERSION` | client → server | Version negotiation handshake |

---

## Serialisation

Payloads use `QDataStream` with `Qt_6_0` data format version.

**QDataStream scalar types** (all big-endian):
- `quint8` / `uint8_t` — 1 byte
- `quint16` — 2 bytes, big-endian
- `quint32` / `uint32_t` — 4 bytes, big-endian
- `int` — 4 bytes, big-endian
- `bool` — 1 byte (0x00 or 0x01)

**`QString`** — 4-byte big-endian character count, followed by UTF-16BE data.
An empty string is serialised as `0x00 0x00 0x00 0x00`. A null `QString`
is `0xFF 0xFF 0xFF 0xFF`.

**`QVariant`** — Qt 6.0 QDataStream variant encoding (type ID + payload).
Common types: `int` (type 2), `double` (type 6), `bool` (type 1),
`QString` (type 10), `float` (type 38).

**`QStringList`** — 4-byte count followed by that many `QString` values.

**`QList<T>`** — 4-byte count followed by that many `T` values.

**`QHash<K, V>`** — 4-byte count followed by (key, value) pairs in
unspecified order.

---

## Connection lifecycle

### 1. SSL handshake

The main port (`:4242`) requires mutual TLS — the client must present a
certificate signed by the server's CA. The control port (`:4243`) requires
only a server certificate (no client cert).

### 2. Version negotiation

The client sends `FRAME_VERSION` immediately after the SSL handshake. No
other frames are accepted until version negotiation completes.

```
Client → Server: FRAME_VERSION
  payload (QDataStream, BigEndian byte order for first two fields):
    quint8  major        — client protocol major version
    quint8  minor        — client protocol minor version
    QString username     — authenticated username
    QString devicename   — device name / identifier
    int     channel      — 0=service, 1=application, 2=cpanel
```

The server responds with `FRAME_SYSTEM` "CONNECTED" (success) or
`FRAME_SYSTEM` "ERROR: <reason>" followed by disconnect.

After a successful `CONNECTED`, the server immediately sends:
```
Server → Client: FRAME_SYSTEM "APP:<md5base64>"
```
where `<md5base64>` is the base64-encoded MD5 checksum of the client bundle.

### 3. Application lifecycle

After version negotiation, the client may send any combination of commands.
Typical sequence:

```
Client → Server: FRAME_SYSTEM "OBJLIST?"
Server → Client: FRAME_OBJLIST  (compressed full snapshot)

Client → Server: FRAME_SYSTEM "OBJ:meas.temperature.outside:0"
Server → Client: FRAME_SYSTEM "OBJDESC:meas.temperature.outside:°C"
Server → Client: FRAME_OBJVALUE  (initial value for object 0)

  [normal operation — server pushes value changes]
Server → Client: FRAME_OBJVALUE  (object index + new value)
Client → Server: FRAME_OBJVALUE  (client writes a value)

  [keepalive]
Client → Server: FRAME_SYSTEM "PING"
Server → Client: FRAME_SYSTEM "PONG"
```

---

## FRAME_VERSION (255)

See connection lifecycle above. `FRAME_VERSION` uses `QDataStream::BigEndian`
for the two version bytes; all other frames use the platform default
(also big-endian for Qt integer types).

---

## FRAME_SYSTEM (0)

Payload: UTF-8 encoded string. Commands are colon-delimited: `VERB:arg1:arg2`.

### Client → Server commands

| Command | Format | Description |
|---|---|---|
| `APP?` | `APP?` | Request the client bundle file transfer |
| `OBJLIST?` | `OBJLIST?` | Request the full compressed object list |
| `DMZ` | `DMZ` | Subscribe to all objects (DMZ mode) |
| `OBJ` | `OBJ:<name>:<index>` | Subscribe to object `<name>` using numeric `<index>` for subsequent updates |
| `ALARMS` | `ALARMS:<username>` | Request the current active alarms list |
| `LISTOBJECTS` | `LISTOBJECTS` | Request a comma-separated list of all object names |
| `PING` | `PING` | Keepalive |
| `POSITION` | `POSITION:<lat>:<lon>:<alt>:<accuracy>:<provider>` | Report GPS position from the client |

**Notes on `OBJ`**: once subscribed, the server sends `OBJDESC` and the current
value. Future value changes arrive as `FRAME_OBJVALUE` with the assigned index.
The index is chosen by the client (typically a sequentially assigned quint16).

**Notes on `POSITION`**: decimal values may use either `.` or `,` as the
decimal separator. Valid ranges: latitude [-90, 90], longitude [-180, 180],
accuracy ≥ 0.

### Server → Client commands

| Command | Format | Description |
|---|---|---|
| `CONNECTED` | `CONNECTED` | Version accepted |
| `ERROR: <reason>` | `ERROR: text` | Version rejected (server disconnects after) |
| `APP:<checksum>` | `APP:<md5base64>` | MD5 checksum of the client bundle |
| `OBJDESC:<name>:<unit>` | — | Object descriptor response to `OBJ` subscription |
| `DMZ:OK` | `DMZ:OK` | DMZ mode confirmed |
| `NOTIFY:<message>` | `NOTIFY:text` | Push notification from the server |
| `POSITION?` | `POSITION?` | Server requests GPS position from the client |
| `PONG` | `PONG` | Response to `PING` |
| `ALARM:<base64>` | — | Active alarms, zlib-compressed then base64-encoded |
| `LISTOBJECTS:<csv>` | — | Comma-separated object names |

**`ALARM` payload**: the inner content (before base64) is a plain text list of
alternating title/message lines separated by `\n\n`:
```
Alarm title 1\nAlarm message 1\n\nAlarm title 2\nAlarm message 2\n\n
```

---

## FRAME_OBJVALUE (2)

Used in both directions: server pushes value changes to clients; clients write
new values back to the server.

```
QDataStream payload:
  quint16   objectId    — the index assigned during OBJ subscription
  QVariant  value       — new value
  bool      confirm     — true = request physical confirmation (KNX read-back)
```

After the quint16 object ID, the rest of the payload is serialised with
`QDataStream::Qt_6_0`:

```
  [0..1] quint16  objectId  (big-endian)
  [2..N] QVariant value     (Qt 6.0 variant encoding)
  [N+1]  bool     confirm
```

---

## FRAME_OBJLIST (8)

Full snapshot of all registered objects. Sent in response to `OBJLIST?` and
when a client enables DMZ mode.

Payload: **zlib-compressed** QDataStream.

```
Uncompressed QDataStream content (Qt_6_0):
  QHash<QString, std::pair<QVariant, QString>>
```

Each entry:
- Key: object name (`QString`)
- Value: pair of (current value `QVariant`, unit `QString`)

The hash is serialised as: 4-byte count, then count × (key, first, second).

---

## FRAME_FILE (1)

Transfers a file from server to client. Used for the `APP?` request.

```
Payload (raw bytes, NOT QDataStream):
  bytes [0..2]  — 3-byte ASCII file identifier (e.g. "APP")
  byte  [3]     — separator ':'
  bytes [4..]   — raw file content
```

---

## FRAME_DBQUERY (3)

Client requests execution of a SQL query on the server's database.

```
QDataStream payload:
  uint32_t  queryId  — client-chosen ID for matching the response
  QString   sql      — SQL query text
```

---

## FRAME_DBRESULT (4)

Server returns the result of a previously requested query.

```
QDataStream payload:
  uint32_t               queryId  — matches the request queryId
  QStringList            columns  — column names
  QList<QList<QVariant>> rows     — row data
```

---

## FRAME_HISTORY (9)

Client requests a min/avg/max evolution of a stored measurement over a period,
bucketed by a fixed step, without sending SQL. Added alongside `FRAME_DBQUERY`
for the transition — both coexist; `FRAME_HISTORY` is the intended replacement
for time-series graphing, `FRAME_DBQUERY` remains available for ad-hoc queries.

```
QDataStream payload:
  uint32_t  queryId  — client-chosen ID for matching the response
  QString   domain   — table family, e.g. "temperature" (server-whitelisted)
  QString   object   — KaZa object name, e.g. "meas.temperature.porch"
  QDateTime start    — period start (inclusive)
  QDateTime end      — period end (exclusive)
  quint8    step     — HistoryStep: 0=hour, 1=day, 2=week, 3=month, 4=year
```

**`domain`** selects which table triple to query: `<domain>_raw`,
`<domain>_hourly`, `<domain>_daily`, all following the schema in
`sql/temperature.sql` (`object_id`, `ts`, `value` for raw; `object_id`, `ts`,
`min`, `max`, `avg`, `count` for the rollups). The server only accepts domains
it has been explicitly configured to serve (currently `"temperature"`); any
other value is rejected with an error result rather than executed as SQL —
`domain` is never client-supplied SQL text. Adding a new measurement family
(power, humidity, tank level, …) means creating the three tables with the same
column layout and adding the domain name to the server's whitelist; no
protocol change is required.

**Table selection per `step`** (based on the raw=7 days / hourly=1 month /
daily=indefinite retention policy in `sql/temperature.sql`):

| step | source(s) |
|---|---|
| hour | `<domain>_raw` (last 7 days) ∪ `<domain>_hourly` (older) |
| day | `<domain>_hourly` ∪ `<domain>_daily`, weighted average |
| week / month / year | `<domain>_daily` only, `date_trunc` + weighted average |

---

## FRAME_HISTORYRESULT (10)

Server's reply to `FRAME_HISTORY`.

```
QDataStream payload:
  uint32_t          queryId  — matches the request queryId
  bool              ok       — false if domain/object unknown or query failed
  QString           error    — human-readable reason when ok is false
  QList<QDateTime>  ts       — bucket start timestamps
  QList<double>     min      — minimum value per bucket
  QList<double>     avg      — average value per bucket
  QList<double>     max      — maximum value per bucket
```

`ts`, `min`, `avg`, and `max` are parallel arrays of equal length — index *i*
in one refers to the same bucket as index *i* in the others. No `count` field
is sent: it is only needed server-side to recompute weighted averages across
rollup tables, not for rendering a min/avg/max chart.

---

## FRAME_SOCKET_CONNECT (5)

Client requests the server to open a TCP connection to a target host/port.

```
QDataStream payload:
  uint16_t  socketId  — client-chosen ID for this socket tunnel
  QString   hostname  — target hostname (resolved server-side)
  uint16_t  port      — target port
```

---

## FRAME_SOCKET_DATA (6)

Raw data on a proxied TCP socket. Used in both directions.

```
QDataStream payload:
  uint16_t   socketId
  QByteArray data
```

---

## FRAME_SOCKET_STATE (7)

Server reports a state change on a proxied socket.

```
QDataStream payload:
  uint16_t  socketId
  uint16_t  state     — QAbstractSocket::SocketState value
```

Common states: `0` = UnconnectedState, `2` = HostLookupState,
`3` = ConnectingState, `4` = ConnectedState, `6` = ClosingState.

---

## Implementing a third-party client

Minimum viable client:

1. Establish a mutual-TLS TCP connection to port 4242.
2. Send `FRAME_VERSION` with your credentials.
3. Wait for `FRAME_SYSTEM "CONNECTED"`.
4. Optionally send `FRAME_SYSTEM "OBJLIST?"` for a full snapshot.
5. Subscribe to specific objects with `FRAME_SYSTEM "OBJ:<name>:<id>"`.
6. Process incoming `FRAME_OBJVALUE` to track current values.
7. Send `FRAME_OBJVALUE` to write values.
8. Send `FRAME_SYSTEM "PING"` periodically; verify `PONG` response.

SSL requirements:
- Client certificate signed by the server's CA (`ca.cert.pem`)
- Or use the control port (`:4243`, no client cert required) to obtain the CA
  certificate first.
