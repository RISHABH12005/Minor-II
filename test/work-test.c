#include <stdio.h>
#include <string.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

// WiFi
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"

// Time
#include "time.h"
#include "lwip/apps/sntp.h"

// ---------------- WIFI ----------------
#define WIFI_SSID "V30P"
#define WIFI_PASS "Radon~190"

#define AP_SSID "RFID"
#define AP_PASS "12345678"

// ---------------- PINS ----------------
#define PIN_NUM_MISO 2
#define PIN_NUM_MOSI 7
#define PIN_NUM_CLK  6
#define PIN_NUM_CS   10
#define PIN_NUM_RST  4

spi_device_handle_t spi;

// ---------------- USERS ----------------
typedef struct {
    uint8_t uid[4];
    char name[20];
    int count;
} User;

User users[] = {
    {{0x43,0x4F,0xCB,0xDC}, "Rishabh", 0},
    {{0x53,0x68,0xA5,0xDD}, "Ranjan", 0},
    {{0xE3,0x36,0xD9,0xFC}, "Rashmi", 0},
    {{0xC3,0xD6,0xAF,0xFC}, "Amit", 0}
};

#define USER_COUNT (sizeof(users)/sizeof(User))

// ---------------- WIFI ----------------
void wifi_init() {

    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t sta_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS
        }
    };

    wifi_config_t ap_config = {
        .ap = {
            .ssid = AP_SSID,
            .password = AP_PASS,
            .max_connection = 10,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        }
    };

    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);

    esp_wifi_start();
    esp_wifi_connect();

    printf("WiFi connected + AP started (max 10 devices)\n");
}

// ---------------- TIME ----------------
void obtain_time() {

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "time.google.com");
    sntp_init();

    time_t now = 0;
    struct tm timeinfo = {0};

    int retry = 0;
    const int max_retry = 10;

    while (timeinfo.tm_year < (2020 - 1900) && retry < max_retry) {
        printf("Waiting for time...\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
        time(&now);
        localtime_r(&now, &timeinfo);
        retry++;
    }

    if (retry == max_retry) {
        printf(" Time sync failed (no internet)\n");
    } else {
        printf("Time synchronized!\n");
    }
}

void print_time() {
    struct tm timeinfo;
    time_t now;
    time(&now);
    localtime_r(&now, &timeinfo);

    printf("%04d-%02d-%02d %02d:%02d:%02d",
           timeinfo.tm_year + 1900,
           timeinfo.tm_mon + 1,
           timeinfo.tm_mday,
           timeinfo.tm_hour,
           timeinfo.tm_min,
           timeinfo.tm_sec);
}

// ---------------- SPI ----------------
void rc522_write(uint8_t reg, uint8_t val) {
    uint8_t data[2] = {(reg << 1) & 0x7E, val};
    spi_transaction_t t = {.length = 16, .tx_buffer = data};
    spi_device_transmit(spi, &t);
}

uint8_t rc522_read(uint8_t reg) {
    uint8_t tx[2] = {((reg << 1) & 0x7E) | 0x80, 0x00};
    uint8_t rx[2];

    spi_transaction_t t = {.length = 16, .tx_buffer = tx, .rx_buffer = rx};
    spi_device_transmit(spi, &t);

    return rx[1];
}

// ---------------- RC522 ----------------
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

    rc522_write(0x11, 0x3D);
    rc522_write(0x14, 0x03); // antenna ON

    printf("RC522 READY\n");
}

// ---------------- RFID ----------------
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
        for (int i = 0; i < 5; i++) {
            uid[i] = rc522_read(0x09);
        }
        return 1;
    }
    return 0;
}

// ---------------- MAIN ----------------
void app_main() {

    wifi_init();
    vTaskDelay(pdMS_TO_TICKS(5000));

    obtain_time();

    rc522_init();
    rc522_write(0x14, 0x03); // ensure antenna ON again

    uint8_t uid[5];
    uint8_t last_uid[5] = {0};

    while (1) {

        printf("Scanning...\n");

        if (rc522_detect()) {

            printf("CARD FOUND\n");

            if (rc522_read_uid(uid)) {

                if (memcmp(uid, last_uid, 4) != 0) {

                    memcpy(last_uid, uid, 4);

                    for (int i = 0; i < USER_COUNT; i++) {

                        if (memcmp(uid, users[i].uid, 4) == 0) {

                            users[i].count++;

                            printf("\nNAME: %s\n", users[i].name);
                            printf("COUNT: %d\n", users[i].count);
                            printf("TIME: ");
                            print_time();
                            printf("\n");

                            break;
                        }
                    }
                }
            }

        } else {
            memset(last_uid, 0, sizeof(last_uid));
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}