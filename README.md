# KaZa Server

**Extensible Home Automation Server based on Qt/QML**

KaZa Server is a headless automation server that runs on Linux systems (x86_64, ARM/Raspberry Pi) and provides a plugin-based architecture for integrating various home automation protocols. It exposes a unified object model synchronized with connected clients via an efficient binary protocol over SSL/TLS.

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-linux-lightgrey.svg)]()
[![Qt](https://img.shields.io/badge/Qt-6.2%2B-green.svg)]()

## Features

- **Plugin Architecture:** Extend with KNX, MQTT, Modbus, and custom protocol integrations
- **QML-Based:** Server-side automation logic written in declarative QML
- **Efficient Protocol:** Binary, frame-based protocol optimized for low bandwidth
- **Secure by Default:** Mutual TLS authentication with client certificates
- **Cross-Platform Clients:** Support for Linux, Windows, and Android clients
- **Time-Based Automation:** Built-in cron-like scheduler for time-triggered events
- **Database Integration:** Optional PostgreSQL support for logging and time-series data
- **Systemd Integration:** Native systemd service with notify support
- **Debian Packages:** Easy installation and updates via APT repository

## Quick Start

### Installation

```bash
# Add KaZa repository
wget -qO - https://repo.example.com/kaza/gpg.key | sudo apt-key add -
echo "deb https://repo.example.com/kaza stable main" | \
    sudo tee /etc/apt/sources.list.d/kaza.list

# Install server and plugins
sudo apt update
sudo apt install kaza-server kaza-mqtt kaza-knx
```

### Configuration

```bash
# Generate SSL certificates
cd /etc/kaza
sudo /usr/share/kaza/tools/make_certificate.sh \
    "your-server.local" \
    "client-password" \
    "server-password"

# Edit configuration
sudo nano /etc/KaZaServer.conf

# Create your automation logic
sudo mkdir -p /opt/kaza
sudo nano /opt/kaza/main.qml
```

### Start Service

```bash
# Start and enable service
sudo systemctl enable kaza-server
sudo systemctl start kaza-server

# Check status
sudo systemctl status kaza-server
sudo journalctl -u kaza-server -f
```

## Example: Basic Automation

```qml
import QtQml 2.0
import org.kazoe.kaza 1.0
import org.kazoe.knx 1.0

KaZaElement {
    // Connect to KNX bus
    KnxBus {
        id: knx
        knxd: "ip:192.168.1.250"
        knxProj: "/etc/kaza/home.knxproj"
    }

    // Reference KNX objects
    KzObject {
        id: livingRoomLight
        object: "knx.lights.living_room"
    }

    KzObject {
        id: motionSensor
        object: "knx.sensors.entrance.motion"
        onValueChanged: {
            if (value) livingRoomLight.set(true)
        }
    }

    // Time-based automation
    Scheduler {
        hour: "20"
        minute: "0"
        onTimeout: {
            console.log("Evening mode")
            livingRoomLight.set(true)
        }
    }
}
```

## Documentation

### Getting Started

1. **[Architecture Overview](doc/01-architecture.md)**
   Understanding KaZa Server's architecture, components, and design philosophy

2. **[Installation Guide](doc/02-installation.md)**
   Complete installation instructions for Debian/Ubuntu and Raspberry Pi

3. **[Configuration Guide](doc/03-configuration.md)**
   Detailed configuration reference for `/etc/KaZaServer.conf`

### Development

4. **[API Reference](doc/04-api-reference.md)**
   QML types reference: `KaZaElement`, `KaZaObject`, `KzObject`, `Scheduler`

5. **[User Application Development](doc/05-user-application.md)**
   Creating automation logic with server-side QML

6. **[Plugin Development](doc/06-plugin-development.md)**
   Extending KaZa Server with custom protocols and integrations

### Advanced Topics

7. **[Protocol Specification](doc/07-protocol.md)**
   Binary protocol specification for client-server communication

8. **[SSL Certificate Management](doc/08-ssl-certificates.md)**
   Generating, managing, and renewing SSL/TLS certificates

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                  User Application (QML)                  │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐    │
│  │  KNX Plugin │  │ MQTT Plugin │  │   Custom    │    │
│  └─────────────┘  └─────────────┘  └─────────────┘    │
└───────────────────────┬─────────────────────────────────┘
                        │ QML Types
┌───────────────────────┼─────────────────────────────────┐
│               KaZa Server Core                           │
│  ┌──────────────────┐ │ ┌────────────┐                  │
│  │   KaZaManager    ├─┴─┤QQmlEngine │                  │
│  └────┬─────────────┘   └────────────┘                  │
│       │                                                  │
│  ┌────┴─────┬──────────────┬──────────┬──────────┐     │
│  │  SSL     │    SCPI      │PostgreSQL│ systemd  │     │
│  │  Server  │    Server    │ Database │  notify  │     │
│  └──────────┴──────────────┴──────────┴──────────┘     │
└──────────────────────────────────────────────────────────┘
```

## Available Plugins

### Official Plugins

- **kaza-knx** - KNX bus integration via knxd (ETS project import support)
- **kaza-mqtt** - MQTT broker integration with topic subscriptions
- **kaza-helios** - Helios ventilation system integration
- **kaza-palazzetti** - Palazzetti pellet stove integration

### Creating Custom Plugins

See [Plugin Development Guide](doc/06-plugin-development.md) for creating your own integrations.

## System Requirements

### Server (Minimum)

- **OS:** Debian 11+, Ubuntu 22.04+, or Raspberry Pi OS
- **CPU:** 1 GHz single-core (ARM or x86_64)
- **RAM:** 512 MB
- **Storage:** 1 GB
- **Network:** Ethernet or WiFi

### Server (Recommended)

- **CPU:** 1.5 GHz dual-core or better
- **RAM:** 1 GB or more
- **Storage:** 4 GB or more (for database logging)
- **Network:** Gigabit Ethernet

### Dependencies

- Qt 6.2+ (Qt 6.8+ recommended)
- OpenSSL 1.1.1+ or 3.0+
- systemd
- PostgreSQL 12+ (optional, for database features)

## Building from Source

```bash
# Install dependencies
sudo apt install build-essential cmake git \
    qt6-base-dev qt6-declarative-dev \
    libqt6sql6 qml6-module-qtqml \
    libsystemd-dev

# Clone repository
git clone https://github.com/yourusername/kaza-server.git
cd kaza-server
git submodule update --init --recursive

# Build
mkdir build && cd build
cmake ..
make -j$(nproc)

# Create Debian package
cpack -G DEB

# Install
sudo dpkg -i kaza-server-*.deb
```

## Project Structure

```
KaZa-Server/
├── src/               # Server source code
│   ├── main.cpp       # Entry point
│   ├── kazamanager.*  # Main manager
│   ├── kazaobject.*   # Object base class
│   ├── kazaelement.*  # QML root element
│   ├── kzobject.*     # Object reference
│   ├── scheduler.*    # Time-based scheduler
│   └── cmd/           # Command-line tools
├── protocol/          # Protocol implementation
│   ├── kazaprotocol.h
│   └── kazaprotocol.cpp
├── conf/              # Configuration files
│   ├── KaZaServer.service  # Systemd unit
│   ├── postinst       # Post-install script
│   └── postrm         # Post-remove script
├── tools/             # Utility scripts
│   └── make_certificate.sh  # SSL cert generation
├── cmake/             # CMake modules
├── doc/               # Documentation
├── CMakeLists.txt     # Build configuration
├── LICENSE            # GPL v3 license
└── README.md          # This file
```

## Configuration File

`/etc/KaZaServer.conf`:

```ini
[Server]
qml=/opt/kaza/main.qml

[Client]
app=/var/lib/kaza/client.rcc
host=home.example.com

[ssl]
port=1756
ca_file=/etc/kaza/ssl/ca.cert.pem
cert_file=/etc/kaza/ssl/server.cert
private_key_file=/etc/kaza/ssl/server.key
private_key_password=YourPassword

[scpi]
port=43500
password=AdminPassword

[database]
driver=QPSQL
hostname=127.0.0.1
port=5432
dbName=kaza
username=kaza
password=DatabasePassword
```

See [Configuration Guide](doc/03-configuration.md) for details.

## Support & Community

- **Issues:** [GitHub Issues](https://github.com/yourusername/kaza-server/issues)
- **Documentation:** [doc/](doc/)
- **Website:** https://kaza.kazoe.org (TBD)

## Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## Related Projects

- **KaZa-Client** - Multi-platform client application (Linux, Windows, Android)
- **KaZa-Interface** - Admin/debug tool (Qt Widgets, in development)
- **KaZa-Protocol** - Shared protocol library

## License

This project is licensed under the GNU General Public License v3.0 - see the [LICENSE](LICENSE) file for details.

## Authors

- Fabien Proriol <fabien.proriol@kazoe.org>

## Acknowledgments

- Qt Project for the excellent Qt framework
- KNX Association for the KNX standard
- Eclipse Paho for MQTT client library
- All contributors and users of KaZa

---

**Made with ❤️ for home automation enthusiasts**
