#include <stdio.h>
#include <string.h>

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "mqtt_client.h"

// ---------------- WIFI ----------------
#define WIFI_SSID "V30P"
#define WIFI_PASS "Radon~190"
#define MQTT_BROKER_URI "mqtt://10.70.40.202"

static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static int client_count = 0;
static int last_client_count = -1;

// ---------------- RC522 ----------------
#define PIN_NUM_MISO 2
#define PIN_NUM_MOSI 7
#define PIN_NUM_CLK  6
#define PIN_NUM_CS   10
#define PIN_NUM_RST  4

spi_device_handle_t spi;

// ---------------- UID MAP ----------------
typedef struct {
    uint8_t uid[4];
    const char *name;
} card_t;

card_t cards[] = {
    {{0x53, 0x68, 0xA5, 0xDD}, "Amit"},
    {{0xE3, 0x36, 0xD9, 0xFC}, "Rishabh"},
};

// ---------------- MQTT ----------------
esp_mqtt_client_handle_t mqtt_client;

// ---------------- RC522 ----------------
void rc522_write(uint8_t reg, uint8_t val) {
    uint8_t data[2] = {(reg<<1)&0x7E, val};
    spi_transaction_t t = {.length=16, .tx_buffer=data};
    spi_device_transmit(spi, &t);
}

uint8_t rc522_read(uint8_t reg) {
    uint8_t tx[2] = {((reg<<1)&0x7E)|0x80, 0};
    uint8_t rx[2];
    spi_transaction_t t = {.length=16, .tx_buffer=tx, .rx_buffer=rx};
    spi_device_transmit(spi, &t);
    return rx[1];
}

void rc522_init() {
    gpio_set_direction(PIN_NUM_RST, GPIO_MODE_OUTPUT);

    spi_bus_config_t bus = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK
    };

    spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t dev = {
        .clock_speed_hz = 500000,
        .mode = 0,
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 1
    };

    spi_bus_add_device(SPI2_HOST, &dev, &spi);

    gpio_set_level(PIN_NUM_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_NUM_RST, 1);

    rc522_write(0x11, 0x3D);
    rc522_write(0x14, rc522_read(0x14) | 0x03);

    printf("RC522 INIT DONE\n");
}

int rc522_detect() {
    rc522_write(0x01, 0x00);
    rc522_write(0x04, 0x7F);
    rc522_write(0x0A, 0x80);

    rc522_write(0x09, 0x26);
    rc522_write(0x01, 0x0C);
    rc522_write(0x0D, 0x87);

    vTaskDelay(pdMS_TO_TICKS(50));

    return (rc522_read(0x04) & 0x30);
}

int rc522_read_uid(uint8_t *uid) {
    rc522_write(0x01, 0x00);
    rc522_write(0x04, 0x7F);
    rc522_write(0x0A, 0x80);

    rc522_write(0x09, 0x93);
    rc522_write(0x09, 0x20);

    rc522_write(0x01, 0x0C);
    rc522_write(0x0D, 0x80);

    vTaskDelay(pdMS_TO_TICKS(50));

    if (rc522_read(0x04) & 0x30) {
        for (int i = 0; i < 4; i++)
            uid[i] = rc522_read(0x09);
        return 1;
    }
    return 0;
}

// ---------------- NAME ----------------
const char* get_name(uint8_t *uid) {
    for (int i = 0; i < sizeof(cards)/sizeof(cards[0]); i++) {
        if (memcmp(uid, cards[i].uid, 4) == 0)
            return cards[i].name;
    }
    return "Unknown";
}

// ---------------- CLIENT MONITOR ----------------
void publish_clients() {

    wifi_sta_list_t list;
    esp_wifi_ap_get_sta_list(&list);

    if (list.num == last_client_count)
        return;

    last_client_count = list.num;

    printf("\n===== CONNECTED DEVICES =====\n");
    printf("Total Clients: %d\n", list.num);

    char payload[256] = "{ \"clients\": [";

    for (int i = 0; i < list.num; i++) {

        wifi_sta_info_t s = list.sta[i];

        printf("Device %d → MAC: %02X:%02X:%02X:%02X:%02X:%02X | RSSI: %d\n",
               i + 1,
               s.mac[0], s.mac[1], s.mac[2],
               s.mac[3], s.mac[4], s.mac[5],
               s.rssi);

        char entry[64];
        sprintf(entry,
            "{\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\",\"rssi\":%d}",
            s.mac[0],s.mac[1],s.mac[2],
            s.mac[3],s.mac[4],s.mac[5],
            s.rssi
        );

        strcat(payload, entry);
        if (i < list.num - 1) strcat(payload, ",");
    }

    strcat(payload, "] }");

    esp_mqtt_client_publish(mqtt_client, "rfid/clients", payload, 0, 1, 0);

    printf("================================\n\n");
}

// ---------------- WIFI ----------------
static void wifi_event_handler(void* arg, esp_event_base_t base,
                              int32_t id, void* data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
        esp_wifi_connect();

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
        esp_wifi_connect();

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        printf("WiFi GOT IP\n");
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }

    if (id == WIFI_EVENT_AP_STACONNECTED) {
        client_count++;
        printf("Client Connected: %d\n", client_count);
    }

    if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        client_count--;
        if (client_count < 0) client_count = 0;
        printf("Client Disconnected: %d\n", client_count);
    }
}

void wifi_init() {

    wifi_event_group = xEventGroupCreate();

    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);

    wifi_config_t sta = {.sta = {.ssid=WIFI_SSID, .password=WIFI_PASS}};
    wifi_config_t ap  = {.ap  = {.ssid="RFID", .password="12345678", .max_connection=10}};

    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_STA, &sta);
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    esp_wifi_start();
}

// ---------------- MQTT ----------------
void mqtt_init() {
    esp_mqtt_client_config_t config = {
        .broker.address.uri = MQTT_BROKER_URI,
    };

    mqtt_client = esp_mqtt_client_init(&config);
    esp_mqtt_client_start(mqtt_client);
}

// ---------------- MAIN ----------------
void app_main() {

    wifi_init();

    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);

    printf("WiFi Ready → MQTT\n");

    mqtt_init();
    rc522_init();

    uint8_t uid[4];
    uint8_t last_uid[4] = {0};

    while (1) {

        publish_clients();

        if (rc522_detect()) {

            if (rc522_read_uid(uid)) {

                if (memcmp(uid, last_uid, 4) != 0) {

                    memcpy(last_uid, uid, 4);

                    const char* name = get_name(uid);
                    const char* status = strcmp(name,"Unknown")==0 ? "ALERT":"OK";

                    printf("Detected: %s (%s)\n", name, status);

                    char payload[160];
                    sprintf(payload,
                        "{\"name\":\"%s\",\"uid\":\"%02X%02X%02X%02X\",\"status\":\"%s\"}",
                        name,
                        uid[0], uid[1], uid[2], uid[3],
                        status
                    );

                    esp_mqtt_client_publish(mqtt_client, "rfid/scan", payload, 0, 1, 0);

                    if (strcmp(status, "ALERT") == 0) {
                        esp_mqtt_client_publish(mqtt_client, "rfid/alert", payload, 0, 1, 0);
                    }
                }
            }

        } else {
            memset(last_uid, 0, 4);
        }

        vTaskDelay(pdMS_TO_TICKS(300));
    }
}