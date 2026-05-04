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
    {{0x43, 0x4F, 0xCB, 0xDC}, "Rishabh"},
    {{0x53, 0x68, 0xA5, 0xDD}, "Ranjan"},
    {{0xE3, 0x36, 0xD9, 0xFC}, "Rashmi"},
    {{0xC3, 0xD6, 0xAF, 0xFC}, "Amit"},
};

// ---------------- MQTT ----------------
esp_mqtt_client_handle_t mqtt_client;

// ---------------- RC522 LOW LEVEL ----------------
void rc522_write(uint8_t reg, uint8_t val) {
    uint8_t data[2] = { (reg << 1) & 0x7E, val };
    spi_transaction_t t = {.length = 16, .tx_buffer = data};
    spi_device_transmit(spi, &t);
}

uint8_t rc522_read(uint8_t reg) {
    uint8_t tx[2] = { ((reg << 1) & 0x7E) | 0x80, 0x00 };
    uint8_t rx[2] = {0};
    spi_transaction_t t = {.length = 16, .tx_buffer = tx, .rx_buffer = rx};
    spi_device_transmit(spi, &t);
    return rx[1];
}

// ---------------- RC522 INIT ----------------
void rc522_init() {
    gpio_set_direction(PIN_NUM_RST, GPIO_MODE_OUTPUT);

    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
    };

    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 500000,
        .mode = 0,
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 1,
    };

    spi_bus_add_device(SPI2_HOST, &devcfg, &spi);

    gpio_set_level(PIN_NUM_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_NUM_RST, 1);

    rc522_write(0x11, 0x3D);
    rc522_write(0x14, rc522_read(0x14) | 0x03);

    printf("RC522 INIT DONE\n");
}

// ---------------- DETECT ----------------
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

// ---------------- READ UID ----------------
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
        for (int i = 0; i < 4; i++) {
            uid[i] = rc522_read(0x09);
        }
        return 1;
    }

    return 0;
}

// ---------------- UID MATCH ----------------
const char* get_name(uint8_t *uid) {
    for (int i = 0; i < sizeof(cards)/sizeof(cards[0]); i++) {
        if (memcmp(uid, cards[i].uid, 4) == 0)
            return cards[i].name;
    }
    return "Unknown";
}

// ---------------- WIFI ----------------
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
        esp_wifi_connect();

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        printf("WiFi GOT IP\n");
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init() {
    wifi_event_group = xEventGroupCreate();

    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);

    wifi_config_t sta_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &sta_config);
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

    mqtt_init();
    rc522_init();

    uint8_t uid[4];
    uint8_t last_uid[4] = {0};

    while (1) {

        if (rc522_detect()) {

            if (rc522_read_uid(uid)) {

                if (memcmp(uid, last_uid, 4) != 0) {

                    memcpy(last_uid, uid, 4);

                    printf("UID: %02X %02X %02X %02X\n",
                           uid[0], uid[1], uid[2], uid[3]);

                    const char* name = get_name(uid);

                    printf("Detected: %s\n", name);

                    char payload[128];
                    sprintf(payload,
                        "{\"name\":\"%s\",\"uid\":\"%02X%02X%02X%02X\"}",
                        name,
                        uid[0], uid[1], uid[2], uid[3]);

                    esp_mqtt_client_publish(mqtt_client, "rfid/scan", payload, 0, 1, 0);
                }
            }

        } else {
            memset(last_uid, 0, 4);
        }

        vTaskDelay(pdMS_TO_TICKS(300));
    }
}