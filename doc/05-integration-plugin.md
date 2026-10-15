# 05 — Integration Plugins

This document is for developers writing **new KaZa integration plugins** — C++
Qt QML extension modules that connect external buses or services (MQTT, KNX,
Modbus, Zigbee, serial ports, REST APIs, …) to the KaZa object registry.

---

## Concept

A KaZa plugin is a standard **Qt QML extension module** packaged as a shared
library (`.so` on Linux). It provides new QML types that server-side QML files
can import. The daemon never needs to know about specific integrations — it
just loads whatever QML types are imported by `main.qml`.

The key design rule: **integration types that carry data should extend
`KaZaObject`**. This makes the integration object itself a registry entry,
so its value is immediately available to all `KzObject` subscribers and
connected clients, without any extra wiring.

---

## Existing plugins as reference

| Plugin | Module | Repository |
|---|---|---|
| MQTT | `org.kazoe.mqtt 1.0` | `KaZa-Server-Mqtt/` |
| KNX | `org.kazoe.knx 1.0` | `KaZa-Server-Knx/` |

Study these before writing a new one. `MqttTopic` is the cleanest example of
the `KaZaObject` extension pattern.

---

## Plugin structure

```
KaZa-Server-MyBus/
├── CMakeLists.txt
├── qmldir               — QML module descriptor
└── src/
    ├── plugin.h / plugin.cpp       — QQmlEngineExtensionPlugin
    ├── mybusconnection.h/.cpp      — connection manager (QObject)
    └── mybusobject.h/.cpp          — data object (extends KaZaObject)
```

---

## Step 1: CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)
project(MyBusPlugin VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Qml Quick)

# KaZaObject header — adjust path for your build setup
include_directories(/path/to/KaZa-Server/src)

qt_add_qml_module(MyBusPlugin
    URI org.kazoe.mybus
    VERSION 1.0
    SOURCES
        src/plugin.cpp        src/plugin.h
        src/mybusconnection.cpp src/mybusconnection.h
        src/mybusobject.cpp   src/mybusobject.h
)

target_link_libraries(MyBusPlugin PRIVATE
    Qt6::Core
    Qt6::Qml
)

# Install to Qt's QML plugin directory
include(GNUInstallDirs)
install(TARGETS MyBusPlugin
    LIBRARY DESTINATION
    "${CMAKE_INSTALL_LIBDIR}/qt6/qml/org/kazoe/mybus"
)
install(FILES qmldir
    DESTINATION
    "${CMAKE_INSTALL_LIBDIR}/qt6/qml/org/kazoe/mybus"
)

# Optional: build a .deb package
set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
include(CPack)
```

---

## Step 2: qmldir

```
module org.kazoe.mybus
plugin MyBusPlugin
classname MyBusPlugin
```

---

## Step 3: Plugin registration (plugin.h / plugin.cpp)

```cpp
// plugin.h
#pragma once
#include <QQmlEngineExtensionPlugin>

class MyBusPlugin : public QQmlEngineExtensionPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlEngineExtensionInterface_iid)
public:
    void registerTypes(const char *uri) override;
};
```

```cpp
// plugin.cpp
#include "plugin.h"
#include "mybusconnection.h"
#include "mybusobject.h"
#include <QtQml>

void MyBusPlugin::registerTypes(const char *uri)
{
    qmlRegisterType<MyBusConnection>(uri, 1, 0, "MyBusConnection");
    qmlRegisterType<MyBusObject>(uri, 1, 0, "MyBusObject");
}
```

Alternatively, use `QML_ELEMENT` in the class declarations (requires
`qt_add_qml_module` in CMakeLists.txt) to register automatically without
an explicit `registerTypes`.

---

## Step 4: Connection manager

The connection manager handles the external bus lifecycle (connect, reconnect,
parse incoming messages, send outgoing commands).

```cpp
// mybusconnection.h
#pragma once
#include <QObject>
#include <QQmlEngine>

class MyBusObject;

class MyBusConnection : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_DISABLE_COPY_MOVE(MyBusConnection)

    Q_PROPERTY(QString host READ host WRITE setHost NOTIFY hostChanged FINAL)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged FINAL)

public:
    explicit MyBusConnection(QObject *parent = nullptr);

    QString host() const;
    void setHost(const QString &host);
    bool connected() const;

    // Called by MyBusObject instances to subscribe
    void registerObject(MyBusObject *obj);
    void unregisterObject(MyBusObject *obj);

    // Called by MyBusObject to send a command to the bus
    Q_INVOKABLE void send(const QString &address, const QVariant &value);

signals:
    void hostChanged();
    void connectedChanged();

private slots:
    void _tryConnect();
    void _onDataReady();

private:
    QString m_host;
    bool m_connected {false};
    QList<MyBusObject*> m_objects;
    // ... socket / fd / whatever the bus uses
};
```

Key responsibilities:
- Maintain the connection to the external bus (auto-reconnect on failure)
- Parse incoming data and find the right `MyBusObject` to update
- Provide a `send` method for `MyBusObject` to write back to the bus
- Keep `m_objects` to dispatch incoming data to the right object

---

## Step 5: Data object (extends KaZaObject)

This is the central design choice. By extending `KaZaObject`, your integration
object:
- Automatically registers in the KaZa global registry on construction
- Has `name`, `value`, `unit` that are live and reactive
- Is pushed to all connected clients whenever `value` changes
- Can be subscribed to by `KzObject` in server QML

```cpp
// mybusobject.h
#pragma once
#include "kazaobject.h"   // from KaZa-Server/src/

class MyBusConnection;

class MyBusObject : public KaZaObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_DISABLE_COPY_MOVE(MyBusObject)

    Q_PROPERTY(MyBusConnection* connection READ connection
               WRITE setConnection NOTIFY connectionChanged FINAL)
    Q_PROPERTY(QString address READ address
               WRITE setAddress NOTIFY addressChanged FINAL)

public:
    explicit MyBusObject(QObject *parent = nullptr);
    ~MyBusObject() override;

    MyBusConnection* connection() const;
    void setConnection(MyBusConnection *conn);

    QString address() const;
    void setAddress(const QString &addr);

    // Called by MyBusConnection when new data arrives for this address
    void receiveData(const QVariant &newValue);

    // Called by KaZa when QML writes to this object's value
    void changeValue(QVariant value, bool confirm = false) override;

signals:
    void connectionChanged();
    void addressChanged();

private:
    MyBusConnection *m_connection {nullptr};
    QString m_address;
};
```

```cpp
// mybusobject.cpp
#include "mybusobject.h"
#include "mybusconnection.h"

MyBusObject::MyBusObject(QObject *parent)
    : KaZaObject{parent}
{}

MyBusObject::~MyBusObject()
{
    if (m_connection)
        m_connection->unregisterObject(this);
}

void MyBusObject::setConnection(MyBusConnection *conn)
{
    if (m_connection == conn) return;
    if (m_connection) m_connection->unregisterObject(this);
    m_connection = conn;
    if (m_connection) m_connection->registerObject(this);
    emit connectionChanged();
}

void MyBusObject::receiveData(const QVariant &newValue)
{
    // setValue() is inherited from KaZaObject.
    // Calling it updates the registry and notifies all subscribers.
    setValue(newValue);
}

void MyBusObject::changeValue(QVariant value, bool confirm)
{
    // Called when QML writes to this object (e.g., via KzObject::set()).
    // Forward the write to the physical bus.
    if (m_connection)
        m_connection->send(m_address, value);
    // Optionally update the local value optimistically:
    setValue(value);
}
```

### Key methods inherited from KaZaObject

| Method | Direction | Description |
|---|---|---|
| `setValue(QVariant)` | Bus → KaZa | Call this when the bus sends new data. Updates registry, notifies clients. |
| `changeValue(QVariant, bool)` | KaZa → Bus | Override this to forward writes from QML to the physical bus. |
| `value()` | Read | Current value. |
| `name()` / `setName()` | Read/Write | Registry key. |
| `unit()` / `setUnit()` | Read/Write | Display unit. |

---

## Step 6: Usage in server QML

Once installed, users import and use your plugin in server QML:

```qml
import QtQml 2.0
import org.kazoe.kaza 1.0
import org.kazoe.mybus 1.0

KaZaElement {
    id: root

    property MyBusConnection bus: MyBusConnection {
        host: "192.168.1.50"
    }

    MyBusObject {
        connection: root.bus
        address: "holding_register_1"
        name: "mydevice.temperature"
        unit: "°C"
    }

    MyBusObject {
        connection: root.bus
        address: "coil_1"
        name: "mydevice.relay"

        onValueChanged: console.debug("relay:", value)
    }
}
```

---

## MQTT plugin API reference

### MqttClient

```
module: org.kazoe.mqtt 1.0

Properties:
  hostname  : string   — broker address (IP or hostname)
  clientId  : string   — MQTT client identifier
  login     : string   — optional broker username
  password  : string   — optional broker password
  connected : bool     — read-only; true when broker is connected
  debug     : bool     — enables verbose MQTT logging

Invokable methods:
  publish(topic: string, message: string, qos: int, retain: bool)
```

### MqttTopic : KaZaObject

```
module: org.kazoe.mqtt 1.0

Inherits: KaZaObject (name, value, unit, valueChanged)

Additional properties:
  server : MqttClient*  — reference to the broker connection
  topic  : string       — MQTT topic path
```

When the broker publishes on `topic`, `MqttTopic` parses the payload and
calls `setValue()`, notifying all subscribers. Setting `value` from QML
does NOT automatically publish back to the broker — do that manually with
`broker.publish(...)`.

---

## KNX plugin API reference

### KnxBus

```
module: org.kazoe.knx 1.0

Properties:
  knxd     : string  — knxd connection URL
  knxProj  : string  — absolute path to ETS .knxproj file
```

Connects to the `knxd` daemon and parses the `.knxproj` file to map KNX
group addresses to KaZa object names. All group objects in the project are
registered automatically.

#### knxd URL formats

| URL | Protocol |
|---|---|
| `ip:192.168.1.250` | KNXnet/IP tunneling (default port 3671) |
| `ip:192.168.1.250:3671` | KNXnet/IP with explicit port |
| `local:/run/knx` | Unix socket to local knxd |

#### Datapoint types

The KNX plugin reads DPT information from the `.knxproj` file and converts
raw KNX telegram payloads to QVariant values:

| DPT family | QVariant type |
|---|---|
| DPT-1 (1-bit) | `bool` |
| DPT-5 (1-byte unsigned) | `int` (0–255) |
| DPT-9 (2-byte float) | `double` |
| DPT-10 / DPT-11 (time/date) | `QTime` / `QDate` |
| DPT-14 (4-byte float) | `double` |

#### The `confirm` flag in KzObject.set

`kzobj.set(value, true)` sends a KNX **write** telegram followed immediately
by a **read** telegram. The device's response updates the value in the
registry. Use `confirm = true` when the device may clamp or reject the
value (e.g., a thermostat setpoint with min/max limits).

---

## Packaging as a Debian package

Both `KaZa-Server-Knx` and `KaZa-Server-Mqtt` produce `.deb` packages for
ARM64 deployment using CMake CPack:

```bash
cd KaZa-Server-MyBus/build
cmake ..
make
cpack -G DEB
```

This produces `kaza-mybus_0.1_arm64.deb` which installs the `.so` and
`qmldir` to the correct Qt6 QML plugin path.

Install on the server:

```bash
dpkg -i kaza-mybus_0.1_arm64.deb
```

Then import in server QML:

```qml
import org.kazoe.mybus 1.0
```

No daemon restart required if the plugin is installed and the QML is already
importing it. If you add a new `import` statement, `kazad` needs to be
restarted (or the QML reloaded) to pick up the new plugin.

---

## Checklist for a new plugin

- [ ] Plugin extends `QQmlEngineExtensionPlugin` with `Q_PLUGIN_METADATA`
- [ ] Data objects extend `KaZaObject`, not `QObject`
- [ ] `setValue()` called for incoming bus data (not emitting `valueChanged` directly)
- [ ] `changeValue()` overridden to forward QML writes to the physical bus
- [ ] Connection manager handles reconnection automatically (use `QTimer` retry)
- [ ] Destructor unregisters objects from the connection manager
- [ ] `qmldir` file is correct and installed alongside the `.so`
- [ ] Plugin tested in isolation before integrating with KaZa
