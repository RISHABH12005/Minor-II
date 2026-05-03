# MQTT run in the windows 11 using the Eclipse Mosquitto an open source MQTT broker

cd D:\Mosquitto

.\mosquitto.exe -c .\mosquitto.conf -v

net stop mosquitto

PS D:\Mosquitto> .\mosquitto_sub.exe -h 10.70.40.202 -t rfid/scan -v