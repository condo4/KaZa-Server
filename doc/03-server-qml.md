# 03 — Server QML

This document is for developers writing the **server-side QML** — the
automation logic loaded by `kazad` at startup.

---

## Getting started

The entry point is the file configured as `qml/server` in `/etc/kazad.conf`.
Conventionally this file is named `main.qml`:

```qml
// main.qml
import QtQml 2.0
import org.kazoe.kaza 1.0

KaZaElement {
    id: root

    MyHeating   { id: heating  }
    MyLighting  { id: lighting }
    MySchedules { id: schedules }
}
```

Every integration module is a separate `.qml` file placed in the same
directory (or a subdirectory). `main.qml` instantiates them all as children.

The module `org.kazoe.kaza 1.0` provides all the core types. If you use
KNX or MQTT, add the corresponding plugin import to the files that need it.

---

## Core types

### KaZaElement

The base container for every server module. Use it as the root of every `.qml`
file you write. It supports the QML default-property pattern, so you can put
child objects directly inside it without wrapping them in properties.

```qml
KaZaElement {
    id: root
    // All child objects go here directly
}
```

### KaZaObject

Publishes a named value into the global registry. Every connected client can
subscribe to it. Use it when you are **producing** data.

```qml
KaZaObject {
    id: outsideTemp
    name: "meas.temperature.outside"
    unit: "°C"
    // value is set programmatically: outsideTemp.value = 18.5
}
```

| Property | Type | Description |
|---|---|---|
| `name` | `string` | Unique registry key. Must be set before `value`. |
| `value` | `variant` | Current value. Assigning it notifies all subscribers. |
| `unit` | `string` | Display unit shown to clients (`"°C"`, `"W"`, `"%"`). |

**Signal**: `valueChanged()` — fires whenever `value` changes.

### KzObject

A proxy that **subscribes** to an existing `KaZaObject` in the registry.
Use it when you want to **react** to data produced by another module or plugin.

```qml
KzObject {
    id: heating
    object: "device.heating.onoff"

    onValueChanged: {
        console.debug("Heating is now:", value)
    }
}
```

| Property | Type | Description |
|---|---|---|
| `object` | `string` | Registry key to subscribe to. |
| `value` | `variant` | Current value (reactive, read-only). |
| `unit` | `string` | Unit from the target object (read-only). |

| Slot | Description |
|---|---|
| `set(value, confirm = false)` | Write a value to the bound object. `confirm = true` requests physical acknowledgement (KNX). |
| `refresh()` | Re-read the current value from the registry. |
| `rawid()` | Returns the internal numeric ID of the target object (useful as DB foreign key). |

### InternalObject

Like `KaZaObject` but **persisted** to the database across restarts. Use it
for any value that the user configures and that must survive a power cycle:
thresholds, modes, calibration offsets, schedules.

```qml
InternalObject {
    id: heatingTarget
    name: "internal.heating.target"
    initialValue: 19.5
    unit: "°C"
    onValueChanged: applyHeatingLogic()
}
```

The `initialValue` is written only if no persisted value exists yet (first
startup). After that, the database value takes priority.

> **Important**: Always set `name` before `initialValue` in the QML
> declaration. QML property binding order can vary, and the initialization
> logic depends on the name being set first.

### Scheduler

A cron-style periodic trigger. Fields follow cron syntax:

| Syntax | Meaning |
|---|---|
| `"9"` | Exactly at value 9 |
| `"7,11,17"` | At values 7, 11, and 17 |
| `"*/10"` | Every 10 units (step) |
| `""` or omitted | Any value (wildcard) |

```qml
// Every day at 09:00
Scheduler {
    hour:   "9"
    minute: "0"
    onTimeout: startMorningRoutine()
}

// Every 5 minutes
Scheduler {
    minute: "*/5"
    onTimeout: refreshSensors()
}

// Weekdays only (Mon=1 … Fri=5)
Scheduler {
    wday:   "1,2,3,4,5"
    hour:   "7"
    minute: "30"
    onTimeout: wakeUpRoutine()
}

// Can be disabled by binding to a KaZaObject
Scheduler {
    minute: "*/30"
    enable: autoMode.value   // disables the scheduler when autoMode is false
    onTimeout: doSomething()
}
```

All fields: `year`, `month`, `day`, `wday`, `hour`, `minute`, `patern`
(full cron string override), `enable`.

**Signal**: `timeout()`.

### KzAlarm

Declares a persistent push alarm visible on connected clients while `enable`
is `true`.

```qml
KzAlarm {
    enable: boilerTemp.value > 85.0
    title:  "Surchauffe chaudière"
    message: "Température: " + boilerTemp.value.toFixed(1) + " °C"
    admin:  false   // visible to all users
}
```

| Property | Type | Default | Description |
|---|---|---|---|
| `enable` | `bool` | `false` | Alarm is active when `true`. Bind to any expression. |
| `title` | `string` | `""` | Short title shown in the notification. |
| `message` | `string` | `""` | Detailed message. |
| `admin` | `bool` | `false` | If `true`, only admin users see this alarm. |
| `debug` | `bool` | `false` | Debug-only alarm (not shown in release). |

---

## The `kazamanager` context property

`kazamanager` is a global context property (a C++ `KaZaManager` instance)
accessible from all server QML files.

### Send a push notification

```qml
// To all connected clients
kazamanager.notify("Motion detected in the garden")

// To specific users (prefix usernames with /)
kazamanager.notify("/alice /bob Alarm: front door open")
```

### Run a database query

```qml
kazamanager.runDbQuery(
    "INSERT INTO events (name, value, time) VALUES ('door.front', " +
    door.value + ", now())"
)
```

### Read-only query with bound values

```qml
kazamanager.query(
    "SELECT avg(temp) AS t FROM ecowitt WHERE time > :since",
    { since: new Date(Date.now() - 3600 * 1000) },
    function(result) {
        // result = { ok, status, errorCode, columns, rows, truncated }, rows are arrays of values
        if (result.ok)
            console.debug("average:", result.rows[0][0])
    })
```

`query` never blocks: the statement runs on the service worker pool inside a `READ ONLY`
transaction with a timeout, and the callback is called later on the main thread. Rules: one
`SELECT`/`WITH` statement, no comments, every `:name` placeholder must have a matching key in the
bindings (numbers, booleans, strings, dates or null) and nothing else; at most 1000 rows.

> **Security note**: `runDbQuery` performs no sanitisation. Never interpolate
> values that come from external sources (network, client commands) into SQL
> strings. For trusted internal values (KNX sensor readings, internal object
> values) the risk is low, but prefer parameterised queries when available.

---

## Services: letting clients read data (`KzService`)

Client applications must not send SQL. Instead, the server QML declares **services**: named,
parameterised, read-only queries that clients call with key/value parameters. See
[08-services.md](08-services.md) for the full specification and the protocol.

### Declarative service (SQL)

```qml
import org.kazoe.kaza 1.0

KaZaElement {
    KzService {
        name: "ecowitt.history"
        description: "One quantity of the weather station, min/avg/max per step"
        sql: "SELECT date_bin(:step::interval, time, TIMESTAMPTZ '2000-01-01 00:00:00+00') AS time,
                     min(%metric%) AS min, avg(%metric%) AS avg, max(%metric%) AS max
              FROM ecowitt WHERE time >= :start AND time < :end
              GROUP BY 1 ORDER BY 1"
        maxRows: 5000          // default 1000, hard limit 100000
        timeout: 10000         // ms, default 10000, hard limit 60000
        users: ["*"]           // or a list of user names
        params: [
            KzParam { name: "start"; type: KzParam.DateTime; required: true },
            KzParam { name: "end";   type: KzParam.DateTime; required: true },
            KzParam { name: "step";  type: KzParam.Enum; values: ["15 minutes", "1 hour", "1 day"]; defaultValue: "1 hour" },
            KzParam { name: "metric"; type: KzParam.Identifier; values: ["temp", "humidity"]; required: true }
        ]
    }
}
```

- `:name` is a **bound value**. `%name%` is replaced by one entry of the closed `values` list of a
  `KzParam.Identifier` (column or table names). Nothing else is ever concatenated into the statement.
- `KzParam` types: `Int`, `Double`, `Bool`, `String` (`maxLength`, default 256), `DateTime` (UTC),
  `Enum` (bound, must belong to `values`), `Identifier`. Attributes: `required`, `defaultValue`, `min`, `max`.
- `single: true` returns the first cell of the first row as a value instead of a table.
- The declaration is checked when the QML loads. A service with an unsafe or inconsistent declaration
  (several statements, comments, `$` or `?`, not starting with `SELECT`/`WITH`, placeholder without
  `KzParam`, `%name%` without a closed list...) is **not registered** and the reason is logged.
- Every statement runs in a `READ ONLY` transaction with a `statement_timeout`, even a `WITH` that tries
  to write: PostgreSQL refuses it.
- Clients get a status code, never SQL text or driver messages; those stay in the journal.

### Scriptable service (JavaScript handler)

For anything a single statement cannot express, give a `handler` instead of `sql`:

```qml
KzService {
    name: "energy.summary"
    params: [ KzParam { name: "since"; type: KzParam.DateTime; required: true } ]
    handler: function(req) {
        kazamanager.query("SELECT sum(kwh) FROM energy WHERE day >= :d", { d: req.params.since },
            function(r) {
                if (!r.ok) { req.fail("db"); return }
                req.respondValue(r.rows[0][0])
            })
    }
}
```

The handler runs on the **main thread** and must not block. `req.params` are already validated;
`req.user` is the caller. Answer with `req.respondTable(columns, rows)`, `req.respondValue(v)`,
`req.respondEmpty()`, `req.fail("code")` (code in `[a-z0-9_]`, other values become `internal`) or
`req.invalid("param", "message")`; the answer may come later (from a `kazamanager.query` callback or a
`Timer`). Only the first answer counts. An exception in the handler answers `INTERNAL` immediately; no
answer within `timeout` answers `TIMEOUT`.

### Server configuration

Optional section of `/etc/kazad.conf`:

```ini
[services]
workers=2        # SQL connections used by services
maxInFlight=8    # simultaneous requests per client connection
queueLimit=256   # waiting requests, all clients
```

---

## Object naming convention

Names are dotted strings. There are no enforced namespaces — use the following
conventions to avoid collisions between modules:

| Prefix | Purpose | Examples |
|---|---|---|
| `device.*` | Physical device state or command | `device.heating.onoff`, `device.boiler.solar_panel` |
| `meas.*` | Sensor measurements | `meas.temperature.porch`, `meas.power.plugs_floor` |
| `monitor.*` | Energy monitoring data | `monitor.smartmeter.home_activepower` |
| `system.*` | System-level values | `system.home.edf_tarif_current` |
| `internal.*` | Persisted server-side configuration | `internal.heating.target`, `internal.evmode` |
| `blind.*` | Shutter/blind control | `blind.state.lounge`, `blind.goto.bedroom1` |
| `light.*` | Lighting | `light.state.living`, `light.switch.kitchen` |
| `switch.*` | Generic switches | `switch.commutation.filtration_piscine` |
| `configuration.*` | Mode flags | `configuration.mode_manuel.lounge` |
| `user.*` | Auto-created per user | `user.alice.connected`, `user.alice.latitude` |
| `<integration>.*` | Plugin-specific | `victron.battery.soc`, `helios.inInsideTemperature` |

**Rules**:
- Use lower-case with underscores for multi-word parts.
- Be specific enough to avoid collisions (`meas.power.plugs_floor` rather than
  just `plugs`).
- The `internal.*` prefix is reserved for `InternalObject` entries. Do not
  use it for `KaZaObject` (which is not persisted).
- The `user.*` prefix is managed automatically by the server. Do not create
  objects in this namespace manually.

---

## Structuring a multi-module project

Organise related objects into separate `.qml` files, each rooted at
`KaZaElement`. Compose them in `main.qml`:

```
main.qml
├── Heating.qml      ← heating logic and InternalObjects
├── Lighting.qml     ← KNX-backed light objects
├── Victron.qml      ← MQTT-backed solar/battery data
├── Program.qml      ← cross-module automation logic
├── DataLogger.qml   ← database logging
└── Alert.qml        ← all KzAlarm declarations
```

A child module can reference objects from a sibling by using `KzObject`:

```qml
// Program.qml — references objects defined in Victron.qml
KzObject {
    id: solarProduction
    object: "victron.mppt.0.p"   // published by Victron.qml
}
```

If a module needs to expose an object to be accessed by its parent (e.g. for
property bindings), use a `property` alias:

```qml
// Victron.qml
KaZaElement {
    id: root
    property MqttTopic batterySoc: MqttTopic {
        server: broker
        topic: "N/id/battery/512/Soc"
        name: "victron.battery.soc"
        unit: "%"
    }
}
// main.qml
KaZaElement {
    Victron { id: victron }
    // victron.batterySoc.value is accessible here
}
```

---

## Recipes

### React to a value crossing a threshold

```qml
KzObject {
    id: boilerTemp
    object: "device.boiler.tank_top"

    onValueChanged: {
        if (value !== undefined && value > 85) {
            kazamanager.notify("Boiler overheat: " + value.toFixed(1) + "°C")
        }
    }
}
```

### Computed value from multiple sources

```qml
InternalObject {
    name: "internal.target.byd.min"
    unit: "%"
    value: {
        var c = refcolour.value
        return c <= 1 ? reserveBlue.value
             : c == 2 ? reserveWhite.value
             : reserveRed.value
    }
}
```

### Scheduled action with enable flag

```qml
InternalObject {
    id: autoFiltration
    name: "internal.pool.filtration_auto"
    initialValue: true
}

KzObject {
    id: filtrationRelay
    object: "switch.commutation.pool_filter"
}

Scheduler {
    hour:   "7,11,17"
    minute: "0"
    enable: autoFiltration.value
    onTimeout: filtrationRelay.set(1)
}

Scheduler {
    hour:   "9,15,20"
    minute: "0"
    enable: autoFiltration.value
    onTimeout: filtrationRelay.set(0)
}
```

### Daily database cleanup

```qml
Scheduler {
    hour:   "3"
    minute: "0"
    onTimeout: {
        kazamanager.runDbQuery(
            "DELETE FROM datapoints WHERE time < now() - interval '90 days'"
        )
    }
}
```

### Time-series logging for a sensor

```qml
KzObject {
    object: "meas.power.plugs_floor"
    onValueChanged: {
        if (value !== undefined) {
            kazamanager.runDbQuery(
                "INSERT INTO power (gad, power) VALUES (" +
                rawid() + "," + (value / 1000) + ")"
            )
        }
    }
}
```

`rawid()` returns the stable numeric ID of the bound `KaZaObject`, suitable
as a foreign key in time-series tables.

### Periodic batch logging

Log many sensors in a single query every 10 minutes. Avoids per-value inserts
for historical data that doesn't need per-change granularity:

```qml
Scheduler {
    minute: "*/10"
    onTimeout: {
        var q = "SELECT "
        if (temp1.value) q += "insert_datapoint('sensor.temp1', " + temp1.value + "), "
        if (temp2.value) q += "insert_datapoint('sensor.temp2', " + temp2.value + "), "
        q += "NOW()"   // trailing expression to avoid trailing comma
        kazamanager.runDbQuery(q)
    }
}
```

### HTTP request (XMLHttpRequest)

QML supports `XMLHttpRequest` natively:

```qml
function fetchWeatherData() {
    var xhr = new XMLHttpRequest()
    xhr.open("GET", "https://api.example.com/weather/today")
    xhr.onreadystatechange = function() {
        if (xhr.readyState === XMLHttpRequest.DONE && xhr.status === 200) {
            var data = JSON.parse(xhr.responseText)
            weatherTemp.value = data.temperature
        }
    }
    xhr.send()
}

Scheduler {
    minute: "*/15"
    onTimeout: fetchWeatherData()
}
```

### Alerting on a room light left on

```qml
KaZaElement {
    id: root
    property alias object: kobj.object
    property string message
    property int timeout: 30   // minutes

    KzObject { id: kobj }

    Timer {
        running: kobj.value ? true : false
        interval: root.timeout * 60 * 1000
        onTriggered: {
            if (kobj.value)
                kazamanager.notify(root.message)
        }
    }
}
```

Usage in `Alert.qml`:

```qml
KaZaElement {
    AlertLight {
        object: "light.state.bedroom3"
        message: "Leo's bedroom light is still on"
        timeout: 30
    }
    AlertLight {
        object: "light.state.attic"
        message: "Attic light left on"
    }
}
```

---

## MQTT integration (`org.kazoe.mqtt 1.0`)

See [05 — Integration plugins](05-integration-plugin.md) for the full API.
Quick reference:

```qml
import org.kazoe.mqtt 1.0

KaZaElement {
    property MqttClient broker: MqttClient {
        hostname: "192.168.1.10"
        clientId: "kaza-home"
    }

    // Subscribe: value updates automatically, registers in KaZa registry
    MqttTopic {
        server: broker
        topic: "home/sensor/outdoor_temp"
        name:  "meas.temperature.outdoor"
        unit:  "°C"
    }

    // Publish when a KaZa object changes
    KaZaObject {
        name: "device.heater.setpoint"
        onValueChanged: broker.publish("home/heater/setpoint", value, 0, false)
    }
}
```

**`MqttTopic` extends `KaZaObject`** — no extra wrapper needed. The topic
IS the registry object.

---

## KNX integration (`org.kazoe.knx 1.0`)

See [05 — Integration plugins](05-integration-plugin.md) for the full API.
Quick reference:

```qml
import org.kazoe.knx 1.0

KaZaElement {
    KnxBus {
        knxd: "ip:192.168.1.250"
        knxProj: "/etc/kazad/MyHome.knxproj"
    }

    // All KNX group objects from the .knxproj are auto-registered.
    // Use KzObject to subscribe.
    KzObject {
        id: livingLight
        object: "light.state.living"
        onValueChanged: console.debug("living light:", value)
    }

    // Write to KNX
    KzObject {
        id: heatingCmd
        object: "device.heating.onoff"
    }
    // heatingCmd.set(1)         normal write
    // heatingCmd.set(1, true)   write + request read-back confirmation
}
```
