# 01 — Concept

## What is KaZa?

KaZa is a home-automation platform designed around three ideas:

1. **Logic lives in QML, not in compiled code.** Automation rules, schedules,
   and integrations are written as QML files loaded by the daemon at runtime.
   Changing an automation rule does not require recompiling or restarting the
   daemon — edit the QML file and reload.

2. **The server owns the data; the client owns the display.** All sensor
   readings, device states, and computed values are held in a central object
   registry on the server. Clients connect, subscribe to objects, and render
   whatever the server says. The client UI is itself served by the server — a
   connected client automatically gets the latest UI.

3. **Integration is modular.** Each bus or external service (KNX, MQTT,
   Modbus, …) is a standalone Qt QML plugin. Installing a new integration
   means dropping a `.so` file into the Qt plugin directory and importing it
   in a QML file. No changes to the daemon are required.

---

## The reactive data model

The central concept in KaZa is the **object registry**: a flat, named
key-value store. Every piece of data in the system — a KNX sensor reading, an
MQTT topic value, a computed result, a user-configured threshold — lives in
the registry as a named object.

Names follow a dotted hierarchy by convention:

```
meas.temperature.porch       → outside temperature sensor
device.heating.onoff         → heating relay state
internal.heating.target      → user-configured target temperature
user.alice.connected         → whether Alice's phone is connected
```

The reactive part: when an object's value changes (e.g., a new KNX telegram
arrives), every QML binding that references it updates automatically. You write
your automation logic as declarative expressions:

```qml
// This InternalObject recomputes automatically whenever
// kEdfTarifCurrent or any referenced object changes.
InternalObject {
    name: "internal.target.byd.min"
    value: refcolour.value <= 1 ? reserveBlue.value
         : refcolour.value == 2 ? reserveWhite.value
         : reserveRed.value
}
```

No event handlers, no polling, no explicit signal/slot wiring for simple data
transformations. The QML binding engine handles it.

---

## Why QML for a headless server?

QML is typically associated with graphical applications. KaZa uses only the
non-graphical part of the QML stack:

- **The binding engine**: reactive property evaluation, the `onValueChanged`
  signal, and declarative expressions are all pure data-flow mechanisms with
  no rendering dependency.
- **The JavaScript engine**: V4 (Qt's ECMAScript engine) executes the logic
  functions embedded in QML items.
- **The component model**: the `import` system and QML item hierarchy give a
  clean way to organise automation modules into files.

The result is a configuration language that is far more expressive than YAML
(which cannot express reactive dependencies at all) and more integrated than
Python scripts (which need manual signal/slot wiring), while remaining readable
by someone who has never written C++.

---

## Comparison with alternatives

### Home Assistant

| | KaZa | Home Assistant |
|---|---|---|
| Language | C++ daemon + QML config | Python daemon + YAML config |
| Automation logic | QML reactive bindings | YAML automations + Python scripts |
| UI runtime | Native Qt (compiled, 60 fps) | Browser (React/TypeScript) |
| UI deployment | Served by daemon, auto-updated | Lovelace cards, updated via HA |
| Integration | Qt QML plugin (.so) | Python package (pip install) |
| Ecosystem | Small, focused | Thousands of community integrations |
| Resource use | Very light (embeddable on ARM) | Heavy (Python + database + web server) |

KaZa is a better fit when you want a lightweight, low-latency daemon on an
embedded ARM board (Raspberry Pi, etc.) with a native mobile/desktop client.
Home Assistant is better when you need a large ecosystem of ready-made
integrations and are comfortable with its Python/YAML model.

### Node-RED

Node-RED uses a visual flow-based programming model. KaZa's QML is textual
and version-control-friendly. KaZa also has a native binary protocol and client
application, which Node-RED does not.

### OpenHAB

OpenHAB uses a Java-based rules engine and its own DSL. KaZa is lighter and
uses standard Qt tooling for both the daemon and any C++ extensions.

---

## Key design decisions and their consequences

### Decision: serve the client UI from the daemon

**Consequence — pro**: deploying a UI update requires only rebuilding the
resource bundle (`app.rcc`) and placing it on the server. All connected clients
receive the new UI automatically on next connection. No app store submission,
no manual update on each device.

**Consequence — con**: the client must trust the server completely. The server
can push arbitrary QML code to the client and it will execute. This is
acceptable on a private home network but would require code-signing for
deployment on untrusted networks.

### Decision: flat object registry with dotted names

**Consequence — pro**: extremely simple API. Any code anywhere in the system
can read or write any object by name. No discovery protocol needed. Cross-module
references are trivial (`KzObject { object: "module.other.value" }`).

**Consequence — con**: name collisions are silent. Two modules registering
the same name will overwrite each other without warning. The naming convention
(see [03 — Server QML](03-server-qml.md)) is the only guard.

### Decision: integration = QML plugin

**Consequence — pro**: the daemon never needs to know about specific buses.
KNX and MQTT are not built into `kazad`; they are plugins loaded because the
QML config imports them. A new integration has no surface area in the core.

**Consequence — con**: a plugin that crashes will take down the QML engine
(and with it the entire daemon). Plugin quality matters.
