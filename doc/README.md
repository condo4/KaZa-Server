# KaZa Documentation

KaZa is a home-automation platform built on Qt/C++ and QML. This documentation
covers the full ecosystem: the daemon, the protocol, server-side automation
programming, client UI development, and how to write new integration plugins.

---

## Table of contents

| Document | Audience | Description |
|---|---|---|
| [01 — Concept](01-concept.md) | Everyone | What KaZa is, philosophy, comparison with alternatives |
| [02 — Architecture](02-architecture.md) | Everyone | Components, data flow, object registry, protocol overview |
| [03 — Server QML](03-server-qml.md) | Automation developers | Writing server-side QML: objects, rules, scheduling, notifications |
| [04 — Client QML](04-client-qml.md) | UI developers | Writing client-side QML: widgets, navigation, history, packaging |
| [05 — Integration plugins](05-integration-plugin.md) | Plugin developers | Writing C++ integration plugins (MQTT, KNX, custom buses) |
| [06 — Configuration](06-configuration.md) | System administrators | kazad.conf reference, certificates, database setup |
| [07 — Protocol](07-protocol.md) | Client implementors | Binary frame protocol specification |

---

## Quick orientation

```
kazad (daemon)
├── loads /etc/kazad/main.qml  ← your automation logic
├── org.kazoe.kaza 1.0         ← core QML types
├── org.kazoe.knx 1.0          ← KNX plugin (optional)
├── org.kazoe.mqtt 1.0         ← MQTT plugin (optional)
└── serves SSL TCP :4242       ← KaZa-Client connects here

KaZa-Client (Linux / Windows / Android)
├── connects to kazad
├── downloads app.rcc          ← your UI QML, served by kazad
└── runs the UI locally
```

If you are writing **automations** (temperature control, scheduling, alerts),
start with [03 — Server QML](03-server-qml.md).

If you are writing the **mobile/desktop UI**, start with
[04 — Client QML](04-client-qml.md).

If you are writing a **new bus integration** (Modbus, Zigbee, etc.), start with
[05 — Integration plugins](05-integration-plugin.md).
