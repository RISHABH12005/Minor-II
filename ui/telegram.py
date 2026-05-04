import json
import time
import threading
from datetime import datetime

import requests
import paho.mqtt.client as mqtt

# ---------- CONFIG ----------
BROKER = "10.70.40.202"
BOT_TOKEN = "8706925211:AAEh6te4K5dT5mGkKGSFyecVv6OobbAmzlM"
CHAT_ID = 6014210640
ADMIN_ID = 6014210640

# ---------- USERS ----------
users = {
    "5368A5DD": "Amit",
    "E336D9FC": "Rishabh"
}
name_to_uid = {v.lower(): k for k, v in users.items()}

# ---------- STATE ----------
uid_count = {}        # live
max_uid_count = {}    # max (for display)
ok_counter = {}       # replay detection
alert_counter = {}    # ALERT count per UID
blocked = set()

# history store: uid -> {name, ok_count, alert_count}
history_map = {}

# WiFi
wifi_clients = []
known_devices = set()

# Telegram control
last_update_id = 0
last_query_text = ""
last_query_time = 0
QUERY_COOLDOWN = 2

lock = threading.Lock()

# ---------- UTIL ----------
def get_time():
    return datetime.now().strftime("%d/%m/%Y %H:%M:%S")

def send(msg):
    try:
        requests.post(
            f"https://api.telegram.org/bot{BOT_TOKEN}/sendMessage",
            data={"chat_id": CHAT_ID, "text": msg},
            timeout=3
        )
    except Exception as e:
        print("Telegram error:", e)

# ---------- MQTT ----------
def on_msg(client, userdata, msg):
    global wifi_clients, known_devices

    # ---------- WIFI ----------
    if msg.topic == "rfid/clients":
        try:
            data = json.loads(msg.payload.decode())
            wifi_clients = data.get("clients", [])

            current = set()
            for d in wifi_clients:
                mac = d.get("mac")
                rssi = d.get("rssi")

                if not mac:
                    continue

                current.add(mac)

                if mac not in known_devices:
                    known_devices.add(mac)
                    send(
                        f"🚨 WiFi ALERT\n"
                        f"Time : {get_time()}\n"
                        f"MAC : {mac}\n"
                        f"RSSI : {rssi}"
                    )

            known_devices.intersection_update(current)
        except:
            pass
        return

    # ---------- RFID ----------
    try:
        data = json.loads(msg.payload.decode())
    except:
        return

    uid = data.get("uid")
    status = data.get("status")
    name = users.get(uid, "Unknown")

    if not uid or uid in blocked:
        return

    with lock:
        uid_count[uid] = uid_count.get(uid, 0) + 1
        count = uid_count[uid]

        if uid not in max_uid_count or count > max_uid_count[uid]:
            max_uid_count[uid] = count

        # history
        if uid not in history_map:
            history_map[uid] = {"name": name, "ok": 0, "alert": 0}

    # ---------- OK ----------
    if status == "OK":
        print(f"[OK] {name} ({uid}) Count:{count}")

        with lock:
            ok_counter[uid] = ok_counter.get(uid, 0) + 1
            history_map[uid]["ok"] += 1

        if ok_counter[uid] % 5 == 0:
            send(
                f"⚠️ REPLAY ATTACK\n"
                f"Time : {get_time()}\n"
                f"UID : {uid}\n"
                f"Name : {name}\n"
                f"Scans : {ok_counter[uid]}"
            )
        return

    # ---------- ALERT ----------
    print(f"[ALERT] {uid} Count:{count}")

    with lock:
        alert_counter[uid] = alert_counter.get(uid, 0) + 1
        history_map[uid]["alert"] += 1

    if count >= 5:
        blocked.add(uid)
        send(
            f"🚫 AUTO BLOCKED\n"
            f"Time : {get_time()}\n"
            f"UID : {uid}\n"
            f"Name : {name}\n"
            f"Attempts : {count}"
        )
        return

    send(
        f"🚨 THREAT ALERT\n"
        f"Time : {get_time()}\n"
        f"UID : {uid}\n"
        f"Name : {name}\n"
        f"Attempts : {count}"
    )

def mqtt_loop():
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.on_message = on_msg
    client.connect(BROKER, 1883, 60)
    client.subscribe("rfid/scan")
    client.subscribe("rfid/clients")
    client.loop_forever()

# ---------- TELEGRAM ----------
def tg_loop():
    global last_update_id, last_query_text, last_query_time

    url = f"https://api.telegram.org/bot{BOT_TOKEN}/getUpdates"

    while True:
        try:
            res = requests.get(
                url,
                params={"offset": last_update_id + 1, "timeout": 30},
                timeout=35
            ).json()

            for upd in res.get("result", []):
                last_update_id = upd["update_id"]
                msg = upd.get("message")

                if not msg or msg["from"]["id"] != ADMIN_ID:
                    continue

                text = msg.get("text", "")
                lower = text.lower().strip()
                clean = ''.join(c for c in lower if c.isalnum())

                now = time.time()
                if lower == last_query_text and now - last_query_time < QUERY_COOLDOWN:
                    continue

                last_query_text = lower
                last_query_time = now

                # ---------- MENU ----------
                if clean == "menu":
                    send(
                        "📋 Menu:\n"
                        "history → RFID UID history\n"
                        "alert → alert summary\n"
                        "connection → latest WiFi device\n"
                        "count → all WiFi devices\n"
                        "block <UID>\n"
                        "unblock <UID>\n"
                        "<name> → UID info"
                    )
                    continue

                # ---------- NAME ----------
                if clean in name_to_uid:
                    uid = name_to_uid[clean]
                    with lock:
                        count = max_uid_count.get(uid, 0)

                    send(
                        f"UID : {uid}\n"
                        f"Permission : Access\n"
                        f"Count : {count}"
                    )
                    continue

                # ---------- HISTORY ----------
                if clean == "history":
                    msg_out = ""
                    with lock:
                        for uid, data in history_map.items():
                            perm = "Access" if data["alert"] == 0 else "Denied"
                            count = data["ok"] + data["alert"]
                            msg_out += (
                                f"UID : {uid}\n"
                                f"Name : {data['name']}\n"
                                f"Permission : {perm}\n"
                                f"Count : {count}\n\n"
                            )
                    send(msg_out or "No history")
                    continue

                # ---------- ALERT ----------
                if clean == "alert":
                    msg_out = ""
                    with lock:
                        for uid, count in alert_counter.items():
                            msg_out += (
                                f"UID : {uid}\n"
                                f"Permission : Denied\n"
                                f"Count : {count}\n\n"
                            )
                    send(msg_out or "No alerts")
                    continue

                # ---------- CONNECTION ----------
                if clean == "connection":
                    if wifi_clients:
                        d = wifi_clients[0]
                        send(
                            f"Device 1\nMAC : {d.get('mac')}\nRSSI : {d.get('rssi')}"
                        )
                    else:
                        send("No device")
                    continue

                # ---------- COUNT ----------
                if clean == "count":
                    msg_out = ""
                    for i, d in enumerate(wifi_clients, 1):
                        msg_out += (
                            f"Device {i}\n"
                            f"MAC : {d.get('mac')}\n"
                            f"RSSI : {d.get('rssi')}\n\n"
                        )
                    send(msg_out or "No devices")
                    continue

                # ---------- BLOCK ----------
                if lower.startswith("block "):
                    uid = text.split()[1].upper()
                    blocked.add(uid)
                    send(f"Blocked : {uid}")
                    continue

                # ---------- UNBLOCK ----------
                if lower.startswith("unblock "):
                    uid = text.split()[1].upper()
                    blocked.discard(uid)
                    uid_count[uid] = 0
                    ok_counter[uid] = 0
                    send(f"Unblocked : {uid}")
                    continue

                send("Invalid command")

        except Exception as e:
            print("Telegram error:", e)
            time.sleep(1)

# ---------- MAIN ----------
if __name__ == "__main__":
    print("IDS in IoT (ESP32-C5 to RC522)")

    threading.Thread(target=mqtt_loop, daemon=True).start()
    threading.Thread(target=tg_loop, daemon=True).start()

    while True:
        time.sleep(1)