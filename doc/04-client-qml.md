# 04 — Client QML

This document is for developers writing the **client-side QML** — the user
interface that runs inside the KaZa-Client application on Linux, Windows, and
Android.

---

## How the client QML works

The client app (`KaZa-Client`) is a thin shell:

1. It connects to `kazad` over SSL.
2. After authentication, it downloads a Qt resource bundle (`app.rcc`) from
   the server. This bundle contains all your QML files and assets.
3. It loads the entry point from the bundle and renders your UI using Qt Quick.

You never deploy your UI files directly to user devices — you update them on
the server and all clients pick up the new version automatically on their next
connection.

---

## Entry point

The first file loaded is whatever is set as `qml/client` in `kazad.conf`. This
is the path to the `app.rcc` bundle on the server. Inside the bundle, the
entry point is typically `main.qml`:

```qml
// main.qml (your client QML)
import QtQuick 2.9
import QtQuick.Controls 2.15

Page {
    id: window

    // Wire the Android back button to pop the navigation stack
    Component.onCompleted: {
        mainwindow.callBackClose = askClose
    }
    function askClose(close) {
        if (stackView.depth > 1) {
            close.accepted = false
            stackView.pop()
        } else {
            close.accepted = true
        }
    }

    // Toolbar — shown only when connected
    header: Loader {
        active: manager.ready
        source: "KazaToolbar.qml"
    }

    // Waiting screen — shown while connecting
    Loader {
        active: !manager.ready
        source: "WaitConnection.qml"
    }

    // Main navigation stack
    StackView {
        id: stackView
        anchors.fill: parent
        initialItem: Loader {
            active: manager.ready
            source: "HomePage.qml"
        }
    }
}
```

### Page navigation

Use `stackView.push` and `stackView.pop` for navigation:

```qml
// Navigate to a page
stackView.push("SomePage.qml")

// Navigate with properties
stackView.push("HistoryPage.qml", {
    "title":  "Interior temperature",
    "object": "helios.inInsideTemperature",
    "min": 18,
    "max": 28
})

// Go back
stackView.pop()
```

---

## The `manager` context property

`manager` is a `KazaApplicationManager` instance injected into all client QML.

| Property | Type | Description |
|---|---|---|
| `manager.ready` | `bool` | `true` once SSL is established and client QML is loaded |
| `manager.configured` | `bool` | `false` until the server address has been saved |
| `manager.connected` | `bool` | `true` while the SSL socket is open |
| `manager.secured` | `bool` | `true` once the SSL handshake completed successfully |
| `manager.login` | `string` | Authenticated username |
| `manager.homepage` | `string` | Initial QML file served by the server |
| `manager.errorMsg` | `string` | Last connection error message |

The typical pattern: gate all UI behind `manager.ready`. Show a waiting or
error screen while `!manager.ready`.

---

## KzObject (client-side)

The same QML type as the server-side, but backed by the server registry over
the protocol. Any write propagates to the server, which then pushes the new
value to all other connected clients.

```qml
import org.kazoe.kaza 1.0

KzObject {
    id: outsideTemp
    object: "meas.temperature.outside"

    onValueChanged: {
        // Fired whenever the server pushes a new value
        console.debug("Outside:", value, unit)
    }
}
```

| Property | Type | Description |
|---|---|---|
| `object` | `string` | Registry key on the server. |
| `value` | `variant` | Current value. Reactive — updates when server pushes. |
| `unit` | `string` | Unit string from the server object. |

| Slot | Description |
|---|---|
| `set(value, confirm = false)` | Write to the server. `confirm = true` requests physical KNX read-back. |
| `refresh()` | Request the current value from the server explicitly. |

> **Note**: The client `KzObject` does not have `rawid()`. That method exists
> only on the server-side version.

### Handling undefined values

A `KzObject` that has not yet received a value from the server has
`value === undefined`. Always guard before using the value in expressions:

```qml
Text {
    text: temp.value !== undefined
          ? temp.value.toFixed(1) + " °C"
          : "—"
}
```

---

## KzHistory

`KzHistory` is a `QAbstractTableModel` that fetches time-series data from the
server's database. It is useful for history graphs and trend displays.

```qml
import org.kazoe.kaza 1.0

KzHistory {
    id: historyModel
    query: "SELECT date_trunc('day', time) AS day,
                   min(value), avg(value), max(value)
            FROM datapoints
            WHERE name = 'meas.temperature.outside'
              AND time > now() - interval '30 days'
            GROUP BY day
            ORDER BY day"
}
```

| Property | Type | Description |
|---|---|---|
| `query` | `string` | SQL query to execute on the server. |
| `table` | `string` | Read-only: detected table name from the query. |
| `dataStart` | `DateTime` | Earliest timestamp in the result. |
| `dataEnd` | `DateTime` | Latest timestamp in the result. |
| `dataMin` | `int` | Minimum value in the result. |
| `dataMax` | `int` | Maximum value in the result. |
| `selected` | `DateTime` | User-selected point for detail display. |
| `len` | `int` | Number of rows in the result. |

Column indices in the model: `0` = date, `1` = min, `2` = avg, `3` = max.

Use `KzHistory` as the model for a `ChartView` or `TableView` in your history
pages.

---

## KzPort

`KzPort` opens a local TCP server and tunnels connections through the KaZa
SSL connection to a target host/port on the server's network. This is a
transparent TCP proxy.

```qml
import org.kazoe.kaza 1.0

KzPort {
    id: proxy
    hostname: "192.168.1.20"   // target host (resolved server-side)
    port: 502                  // Modbus TCP port
    // proxy.localPort is the local port to connect to
}
```

---

## Packaging the client QML

Client QML files are bundled into a Qt resource file (`app.rcc`). The server
sends this file to clients in response to the `APP?` command.

Typical project layout:

```
MyClient/
├── Makefile           (or CMakeLists.txt)
├── qml.qrc            — Qt resource file listing all .qml and asset files
├── main.qml
├── HomePage.qml
├── KazaToolbar.qml
├── KazaSidebar.qml
├── WaitConnection.qml
├── Widget/
│   ├── LightSwitch.qml
│   ├── Sensor.qml
│   ├── Shutter.qml
│   └── …
└── Images/
    ├── light_on.png
    └── …
```

**`qml.qrc`** example:

```xml
<RCC>
  <qresource prefix="/">
    <file>main.qml</file>
    <file>HomePage.qml</file>
    <file>KazaToolbar.qml</file>
    <file>KazaSidebar.qml</file>
    <file>WaitConnection.qml</file>
    <file>Widget/LightSwitch.qml</file>
    <file>Widget/Sensor.qml</file>
    <file>Widget/Shutter.qml</file>
    <file>Images/light_on.png</file>
    <file>Images/light_off.png</file>
  </qresource>
</RCC>
```

**Build the bundle**:

```bash
rcc --binary -o app.rcc qml.qrc
```

**Deploy**: copy `app.rcc` to the path set as `qml/client` in `kazad.conf`.
All clients download the new bundle on their next connection and compare the
MD5 checksum; if it differs from their cached version, they reload.

---

## Widget patterns

### Read-only sensor display

```qml
// Widget/Sensor.qml
import QtQuick 2.4
import org.kazoe.kaza 1.0

Rectangle {
    property string name     // object key
    property string label    // display label

    KzObject {
        id: sensor
        object: name
    }

    Text {
        text: label + ": " +
              (sensor.value !== undefined
               ? sensor.value.toFixed(1) + " " + sensor.unit
               : "—")
    }
}
```

### Toggle switch (light, relay)

```qml
// Widget/LightSwitch.qml
import QtQuick 2.4
import org.kazoe.kaza 1.0

Item {
    property string name
    property string obj_state:  "light.state."  + name
    property string obj_switch: "light.switch." + name

    KzObject { id: kstate;  object: obj_state  }
    KzObject { id: kswitch; object: obj_switch }

    Image {
        source: kstate.value ? "Images/Light_ON.png" : "Images/Light_OFF.png"
    }

    MouseArea {
        anchors.fill: parent
        onClicked: kswitch.set(kstate.value ? 0 : 1)
    }
}
```

### Shutter / blind

KNX blinds follow a naming convention. All objects share the same `<name>` suffix:

| Object | Description |
|---|---|
| `blind.state.<name>` | Position 0 (open) – 100 (closed) |
| `blind.closed.<name>` | `true` when fully closed |
| `blind.opened.<name>` | `true` when fully open |
| `blind.open_close.<name>` | `set(0)` open, `set(1)` close |
| `blind.step_stop.<name>` | `set(1)` stop |
| `blind.goto.<name>` | Go to position 0–100 |

```qml
KzObject { id: kgoto; object: "blind.goto.lounge" }

Row {
    Button { text: "Open";  onClicked: kgoto.set(0)   }
    Button { text: "Half";  onClicked: kgoto.set(50)  }
    Button { text: "Close"; onClicked: kgoto.set(100) }
}
```

### Dimmer (percentage control)

```qml
KzObject { id: dimmer; object: "light.setvalue.bedroom2" }

Slider {
    from: 0;  to: 100
    value: dimmer.value !== undefined ? dimmer.value : 0
    onMoved: dimmer.set(value)
}
```

### User presence indicator

The server creates `user.<username>.connected` automatically for every
authenticated user. Use it to show who is home:

```qml
Row {
    Repeater {
        model: ["alice", "bob", "carol"]
        delegate: KzObject {
            object: "user." + modelData + ".connected"
            Rectangle {
                width: 16; height: 16; radius: 8
                visible: parent.value ? true : false
                color: ["blue", "red", "green"][index]
            }
        }
    }
}
```

### Navigate to a history page on click

```qml
Button {
    text: "Inside temperature"
    onClicked: stackView.push("HistoryMinAvgMax.qml", {
        "mainTitle": "Interior",
        "object":    "helios.inInsideTemperature",
        "min": 18,
        "max": 28
    })
}
```

### Scene / mode button

A button that activates a scene by writing multiple objects at once:

```qml
Button {
    text: "Movie mode"
    highlighted: !kModeLounge.value && !kModeKitchen.value

    onClicked: {
        kBlindLounge.set(100)
        kModeLounge.set(0, true)
        kModeKitchen.set(0, true)
        kLightSwitchLounge.set(0)
        kLightSwitchDiner.set(0)
        kLightSwitchKitchen.set(0)
    }
}
```

---

## Android specifics

The Android client has background services and the client QML runs in a
slightly different lifecycle.

### App state management

On Android, the client app may be backgrounded by the OS. The connection is
managed accordingly:

```qml
// These are called automatically by the Android lifecycle bridge.
// You do not need to call them yourself from QML.
// manager.suspend()  — called when app goes to background
// manager.resume()   — called when app returns to foreground
```

### Back button

The back button on Android pops the navigation stack. Wire it in `main.qml`:

```qml
Component.onCompleted: {
    mainwindow.callBackClose = function(close) {
        if (stackView.depth > 1) {
            close.accepted = false
            stackView.pop()
        } else {
            close.accepted = true
        }
    }
}
```

### GPS / presence

The Android client sends GPS position frames to the server automatically
via `KaZaTracker`. The server stores the position in the `user.<name>.*`
objects. There is nothing to configure in the client QML for this feature.

---

## Debugging tips

- Set `manager.debug = true` (if exposed) to enable verbose protocol logging.
- Use `console.debug(...)` in QML — output appears in `logcat` on Android
  and in the terminal on Linux/Windows.
- The `kls` CLI tool (`kls` on the server) lets you inspect the object
  registry from a terminal, which is useful to verify server-side values
  before debugging why the UI is not updating.
- If a `KzObject` value is always `undefined`, check that the object name
  is spelled exactly as registered on the server (case-sensitive).
