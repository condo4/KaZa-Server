# 02 — Architecture

## Component overview

```
┌─────────────────────────────────────────────────────────────────┐
│  HOST (ARM64 Linux server, e.g. Raspberry Pi)                   │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │  kazad (Qt/C++ daemon)                                   │   │
│  │                                                          │   │
│  │  ┌─────────────────────────────────────────────────┐    │   │
│  │  │  QML Engine                                     │    │   │
│  │  │  ├── main.qml  (your automation logic)          │    │   │
│  │  │  │   ├── Knx.qml, Victron.qml, Program.qml … │    │   │
│  │  │  │   └── (any .qml file you write)             │    │   │
│  │  │  ├── org.kazoe.kaza 1.0  (built-in types)      │    │   │
│  │  │  ├── org.kazoe.knx 1.0   (KNX plugin)          │    │   │
│  │  │  └── org.kazoe.mqtt 1.0  (MQTT plugin)         │    │   │
│  │  └─────────────────────────────────────────────────┘    │   │
│  │                                                          │   │
│  │  ┌──────────────────┐   ┌──────────────────────────┐    │   │
│  │  │  Object Registry │   │  KaZaManager             │    │   │
│  │  │  (key-value map) │   │  (singleton C++ object)  │    │   │
│  │  └──────────────────┘   └──────────────────────────┘    │   │
│  │                                                          │   │
│  │  ┌──────────────────┐   ┌──────────────────────────┐    │   │
│  │  │  SSL TCP :4242   │   │  SSL TCP :4243 (control) │    │   │
│  │  │  (app clients)   │   │  (cert distribution)     │    │   │
│  │  └──────────────────┘   └──────────────────────────┘    │   │
│  └──────────────────────────────────────────────────────────┘   │
│                                                                  │
│  External buses: knxd (KNX), MQTT broker, …                     │
└─────────────────────────────────────────────────────────────────┘
          │ SSL TCP :4242
          │
┌─────────▼───────────────────────────────────────────────────────┐
│  KaZa-Client (Linux / Windows / Android)                        │
│  ├── KazaApplicationManager  (SSL connection + protocol)        │
│  ├── Downloads app.rcc  ←── served by kazad                     │
│  ├── Loads and runs client QML (your UI)                        │
│  ├── org.kazoe.kaza 1.0 (client variant — KzObject, KzHistory…) │
│  └── Renders native UI via Qt Quick                             │
└─────────────────────────────────────────────────────────────────┘
```

---

## The object registry

The registry is a flat `QHash<QString, KaZaObject*>` owned by `KaZaManager`.

Every `KaZaObject` declared in QML (by name) registers itself automatically.
Plugins that extend `KaZaObject` (e.g. `MqttTopic`) also register themselves.

When an object's `value` changes:
1. All `KzObject` instances bound to that name are notified (server-side).
2. All connected clients that have subscribed to that object receive a
   `FRAME_OBJVALUE` push frame.
3. QML bindings that reference the object via `KzObject` update reactively.

```
KNX telegram arrives
  → KnxBus parses DPT
  → KaZaObject("light.state.living").setValue(true)
  → KzObject instances in server QML fire onValueChanged
  → KaZaConnection pushes FRAME_OBJVALUE to all subscribed clients
  → Client KzObject fires onValueChanged → UI updates
```

This chain happens synchronously on the Qt event loop. For most home-automation
use cases, latency from telegram receipt to UI update is well under 10 ms.

---

## Source code layout

```
KaZa-Server/
├── src/
│   ├── main.cpp                  — daemon entry point
│   ├── kazamanager.cpp/.h        — singleton: registry, servers, users
│   ├── kazaconnection.cpp/.h     — one client connection
│   ├── kazauser.cpp/.h           — per-user state (GPS, presence)
│   ├── kazaobject.cpp/.h         — base registry object
│   ├── kazaelement.cpp/.h        — QML container base type
│   ├── kzobject.cpp/.h           — QML proxy to a registry object
│   ├── internalobject.cpp/.h     — persisted KaZaObject
│   ├── scheduler.cpp/.h          — cron-style timer
│   ├── kzalarm.cpp/.h            — push alarm
│   ├── kazaremoteconnection.cpp/.h — control-port connection
│   ├── kazacertificategenerator.cpp/.h — self-signed SSL cert generation
│   └── cmd/
│       └── kls.cpp               — kls CLI tool
├── protocol/                     — git submodule: shared protocol code
│   ├── kazaprotocol.cpp/.h       — binary frame encode/decode
│   └── README.md
├── doc/                          — this documentation
└── CMakeLists.txt
```

### Integration plugins (separate repositories)

```
KaZa-Server-Knx/
├── src/
│   ├── plugin.cpp/.h             — QQmlEngineExtensionPlugin
│   ├── knxbus.cpp/.h             — KNX bus connection (knxd)
│   └── knxobject.cpp/.h          — KNX group object ↔ KaZaObject

KaZa-Server-Mqtt/
├── src/
│   ├── plugin.cpp/.h
│   ├── mqttclient.cpp/.h         — MQTT broker connection (Paho)
│   └── mqtttopic.cpp/.h          — MQTT topic ↔ KaZaObject (extends it)
```

---

## Connection lifecycle

### Application client (port 4242)

```
Client                              Server
  │                                   │
  │──── SSL handshake (mutual TLS) ──▶│
  │◀─── SSL handshake complete ───────│
  │                                   │
  │──── FRAME_VERSION ────────────────▶│  (username, devicename, channel)
  │◀─── FRAME_SYSTEM "CONNECTED" ─────│  (or "ERROR: ..." + disconnect)
  │◀─── FRAME_SYSTEM "APP:<md5>" ──────│
  │                                   │
  │──── FRAME_SYSTEM "OBJLIST?" ───────▶│  (request full object snapshot)
  │◀─── FRAME_OBJLIST (compressed) ────│
  │                                   │
  │──── FRAME_SYSTEM "OBJ:name:id" ────▶│  (subscribe to individual objects)
  │◀─── FRAME_SYSTEM "OBJDESC:…" ──────│
  │◀─── FRAME_OBJVALUE ────────────────│  (initial value)
  │                                   │
  │       [normal operation]          │
  │◀─── FRAME_OBJVALUE ────────────────│  (pushed on every value change)
  │──── FRAME_OBJVALUE ────────────────▶│  (client writes a value)
  │◀─── FRAME_SYSTEM "NOTIFY:…" ───────│  (push notification)
```

### Control port (port 4243)

Used for certificate distribution and remote management. SSL without client
certificate requirement (`VerifyNone`). Same frame protocol but typically only
`FRAME_FILE` is used to deliver the CA certificate to new clients.

---

## KaZaManager — the C++ singleton

`KaZaManager` is the central C++ object. It:

- Creates the QML engine and loads `main.qml`
- Owns the object registry (`QHash<QString, KaZaObject*> m_objects`)
- Manages SSL servers (`QSslServer m_server`, `m_remotecontrol`)
- Tracks connected users (`QHash<QString, KaZaUser*> m_users`)
- Exposes static APIs used by QML (`registerObject`, `getObject`, `findUser`, …)
- Is injected into QML as the `kazamanager` context property

Key static methods called by the QML types:

```cpp
KaZaManager::registerObject(KaZaObject*)  // called by KaZaObject constructor
KaZaManager::getObject(name)              // lookup by name
KaZaManager::objects()                   // iterate all objects
KaZaManager::findUser(username)           // lookup KaZaUser
```

---

## Users and presence

When a client authenticates, the server creates a `KaZaUser` if one does not
exist. A `KaZaUser` holds:

- The list of active `KaZaConnection` objects for this user
- GPS position (latitude, longitude, altitude, accuracy) received from the
  client's `POSITION` frames
- Five `KaZaObject` entries auto-created in the registry:
  - `user.<name>.connected` — `true` while at least one session is active
  - `user.<name>.latitude`, `.longitude`, `.altitude`, `.accuracy` — GPS data

Server QML can react to presence and position:

```qml
KzObject {
    object: "user.alice.connected"
    onValueChanged: if (!value) kazamanager.notify("Alice disconnected")
}
```

---

## The kls CLI tool

`kls` is a command-line client for inspecting the object registry from a
terminal. It connects to `kazad`, requests the object list, and prints values:

```bash
kls                            # list all objects
kls sensor.temperature.porch   # show a specific object
kls "meas.power.*"             # filter by pattern
```

It uses the same SSL protocol as the graphical client.

---

## SSL and certificates

On first start, if no certificates exist at `/var/lib/kazad/`, `kazad` generates
a self-signed CA and server certificate automatically (using OpenSSL via
`KazaCertificateGenerator`). The client must trust this CA.

Certificate files:
- `/var/lib/kazad/ca.cert.pem` — CA certificate (distribute to clients)
- `/var/lib/kazad/server.cert.pem` — server certificate
- `/var/lib/kazad/server.key` — encrypted private key

The control port (`:4243`) can be used to distribute `ca.cert.pem` to new
clients that do not yet have a trusted certificate.
