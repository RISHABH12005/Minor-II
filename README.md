# IoT Intrusion Detection System

An ESP32-C5 based intrusion detection system that uses an RC522 RFID reader, WiFi client monitoring, MQTT messaging, and a Telegram control interface.

The firmware scans RFID cards, checks each UID against the allowed user list, publishes scan events over MQTT, reports unknown card attempts as alerts, and monitors devices connected to the ESP32-C5 access point.

## Key Features

- RFID card scanning with RC522 over SPI.
- UID-to-user mapping for known cards.
- Unknown-card detection with alert status.
- MQTT publishing for RFID scans, alerts, and connected WiFi clients.
- ESP32-C5 dual WiFi mode:
  - Station mode connects to an existing hotspot.
  - Access point mode allows nearby devices to connect to the ESP32-C5.
- Telegram-based monitoring and control script.
- Auto-blocking logic for repeated unknown UID attempts.
- Replay-detection alert logic for repeated valid scans.

## Hardware Used

- ESP32-C5 development board.
- RC522 RFID reader module.
- RFID cards or tags.
- WiFi network or hotspot.
- MQTT broker, such as Eclipse Mosquitto.
- System running Python for the Telegram/MQTT bridge.

## RC522 Pin Mapping

| RC522 Signal | ESP32-C5 GPIO |
| --- | --- |
| MISO | GPIO 2 |
| MOSI | GPIO 7 |
| SCK | GPIO 6 |
| SDA / CS | GPIO 10 |
| RST | GPIO 4 |

## Project Structure

```text
.
|-- data/
|   |-- info.txt
|   `-- Install.ps1
|-- ids/
|   |-- main/
|   |   |-- ids.c
|   |   `-- idf_component.yml
|   |-- managed_components/
|   |-- CMakeLists.txt
|   |-- dependencies.lock
|   `-- sdkconfig
|-- report/
|-- run/
|   |-- esp32-c5.ps1
|   `-- mosquitto.ps1
|-- test/
|   `-- esp32-c5/
|-- ui/
|   `-- telegram.py
|-- LICENSE
|-- README.md
|-- project-rishabh.bundle
`-- rfid_log.txt
```

## Firmware Overview

The main ESP32-C5 firmware is located at:

```text
ids/main/ids.c
```

It performs these tasks:

1. Initializes WiFi in AP plus STA mode.
2. Connects to the configured WiFi network.
3. Starts the MQTT client.
4. Initializes the RC522 RFID reader.
5. Reads RFID UIDs continuously.
6. Maps known UIDs to user names.
7. Publishes scan events to MQTT.
8. Publishes alerts for unknown cards.
9. Publishes connected WiFi client information when the AP client list changes.

## MQTT Topics

| Topic | Purpose |
| --- | --- |
| `rfid/scan` | Publishes every RFID scan event. |
| `rfid/alert` | Publishes unknown-card alert events. |
| `rfid/clients` | Publishes connected WiFi client MAC and RSSI data. |

Example scan payload:

```json
{
  "name": "Rishabh",
  "uid": "E336D9FC",
  "status": "OK"
}
```

Example client payload:

```json
{
  "clients": [
    {
      "mac": "AA:BB:CC:DD:EE:FF",
      "rssi": -45
    }
  ]
}
```

## Configuration

Before building or running the project, configure these values for your own environment:

- WiFi SSID and password in `ids/main/ids.c`.
- MQTT broker URI in `ids/main/ids.c`.
- ESP32-C5 AP SSID, AP password, and maximum clients in `ids/main/ids.c`.
- Telegram bot token, chat ID, and admin ID in `ui/telegram.py`.
- Allowed UID and user mappings in both:
  - `ids/main/ids.c`
  - `ui/telegram.py`

Do not publish private WiFi passwords, bot tokens, or chat IDs in public repositories.

## ESP-IDF Requirements

- ESP-IDF installed and activated.
- ESP32-C5 target support available in ESP-IDF.
- USB driver installed for the ESP32-C5 board.
- Python available for ESP-IDF tools.

The firmware uses the ESP-IDF component manager dependency:

```yaml
espressif/mqtt: '*'
```

## Build and Flash

Open an ESP-IDF terminal, then run:

```powershell
cd ids
idf.py set-target esp32c5
idf.py menuconfig
idf.py build
idf.py flash monitor
```

If the build folder needs to be cleaned first:

```powershell
idf.py fullclean
idf.py build
```

The helper script in `run/esp32-c5.ps1` lists the common ESP-IDF commands used for this project.

## MQTT Broker

This project can use Eclipse Mosquitto as the MQTT broker.

Example broker commands:

```powershell
cd D:\Mosquitto
.\mosquitto.exe -c .\mosquitto.conf -v
```

Subscribe to RFID scan events:

```powershell
.\mosquitto_sub.exe -h <broker-ip> -t rfid/scan -v
```

The helper script `run/mosquitto.ps1` contains the broker and subscriber commands used during development.

## Telegram Monitoring Script

The Telegram and MQTT bridge is located at:

```text
ui/telegram.py
```

Install Python dependencies:

```powershell
pip install requests paho-mqtt
```

Run the script:

```powershell
python ui/telegram.py
```

The script subscribes to:

- `rfid/scan`
- `rfid/clients`

It sends Telegram messages for:

- Unknown RFID scan attempts.
- Repeated unknown UID attempts.
- Repeated valid-card scans.
- New devices connected to the ESP32-C5 access point.

## Telegram Commands

| Command | Description |
| --- | --- |
| `menu` | Show available commands. |
| `history` | Show RFID UID scan history. |
| `alert` | Show alert summary. |
| `connection` | Show latest connected WiFi device. |
| `count` | Show all connected WiFi devices. |
| `block <UID>` | Block a UID manually. |
| `unblock <UID>` | Unblock a UID and reset counters. |
| `<name>` | Show UID information for a known user name. |

## Testing and Reference Files

The `test/esp32-c5/` folder contains experimental and reference C files for MQTT, RFID UID reading, and ESP32-C5 workflow testing.

The `rfid_log.txt` file stores RFID scan log output from development runs.

## Documentation

- [Report](https://drive.google.com/file/d/1RqRXOcsAUaRGssg3quj3L2y1gh22XqrC/view?usp=drive_link)
- [Presentation](https://drive.google.com/file/d/1QONF-ldNksnUFNiEqGPQbsMe6mwwZ0x7/view?usp=drive_link)

## License

This project is licensed under the terms in `LICENSE`.
