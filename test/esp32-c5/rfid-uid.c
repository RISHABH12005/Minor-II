#include <stdio.h>
#include <string.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "lwip/sockets.h"

// ---------------- PIN CONFIG ----------------
#define PIN_NUM_MISO 2
#define PIN_NUM_MOSI 7
#define PIN_NUM_CLK  6
#define PIN_NUM_CS   10
#define PIN_NUM_RST  4

spi_device_handle_t spi;

// ---------------- RC522 REGISTERS ----------------
#define CommandReg      0x01
#define CommIrqReg      0x04
#define FIFODataReg     0x09
#define FIFOLevelReg    0x0A
#define BitFramingReg   0x0D
#define ModeReg         0x11
#define TxControlReg    0x14

#define PCD_IDLE        0x00
#define PCD_TRANSCEIVE  0x0C

#define PICC_REQIDL     0x26
#define PICC_ANTICOLL   0x93

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

const char* get_name(uint8_t *uid) {
    for (int i = 0; i < sizeof(cards)/sizeof(cards[0]); i++) {
        if (memcmp(uid, cards[i].uid, 4) == 0) {
            return cards[i].name;
        }
    }
    return "Unknown";
}

// ---------------- SPI WRITE ----------------
void rc522_write(uint8_t reg, uint8_t val) {
    uint8_t data[2] = { (reg << 1) & 0x7E, val };

    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = data
    };
    spi_device_transmit(spi, &t);
}

// ---------------- SPI READ ----------------
uint8_t rc522_read(uint8_t reg) {
    uint8_t tx[2] = { ((reg << 1) & 0x7E) | 0x80, 0x00 };
    uint8_t rx[2] = {0};

    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = tx,
        .rx_buffer = rx
    };
    spi_device_transmit(spi, &t);

    return rx[1];
}

// ---------------- RESET ----------------
void rc522_reset() {
    gpio_set_level(PIN_NUM_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_NUM_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

// ---------------- ANTENNA ----------------
void rc522_antenna_on() {
    uint8_t temp = rc522_read(TxControlReg);
    if (!(temp & 0x03)) {
        rc522_write(TxControlReg, temp | 0x03);
    }
}

// ---------------- INIT ----------------
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

    rc522_reset();

    rc522_write(ModeReg, 0x3D);
    rc522_antenna_on();

    printf("RC522 INIT DONE\n");
}

// ---------------- CARD DETECT ----------------
int rc522_detect() {

    rc522_write(CommandReg, PCD_IDLE);
    rc522_write(CommIrqReg, 0x7F);
    rc522_write(FIFOLevelReg, 0x80);

    rc522_write(FIFODataReg, PICC_REQIDL);
    rc522_write(CommandReg, PCD_TRANSCEIVE);
    rc522_write(BitFramingReg, 0x87);

    vTaskDelay(pdMS_TO_TICKS(50));

    uint8_t irq = rc522_read(CommIrqReg);

    return (irq & 0x30);
}

// ---------------- UID READ ----------------
int rc522_read_uid(uint8_t *uid) {

    rc522_write(CommandReg, PCD_IDLE);
    rc522_write(CommIrqReg, 0x7F);
    rc522_write(FIFOLevelReg, 0x80);

    rc522_write(FIFODataReg, PICC_ANTICOLL);
    rc522_write(FIFODataReg, 0x20);

    rc522_write(CommandReg, PCD_TRANSCEIVE);
    rc522_write(BitFramingReg, 0x80);

    vTaskDelay(pdMS_TO_TICKS(50));

    uint8_t irq = rc522_read(CommIrqReg);

    if (irq & 0x30) {
        for (int i = 0; i < 5; i++) {
            uid[i] = rc522_read(FIFODataReg);
        }
        return 1;
    }

    return 0;
}

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
            .ssid = "V30P",
            .password = "Radon~190",
        },
    };

    wifi_config_t ap_config = {
        .ap = {
            .ssid = "RFID",
            .password = "12345678",
            .max_connection = 10,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };

    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);

    esp_wifi_start();

    printf("WiFi Started (AP + STA)\n");
}

// ---------------- TCP SERVER ----------------
int client_sock = -1;

void wifi_server_task() {

    int server_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_fd, 5);

    printf("Server started on port 8080\n");

    while (1) {

        client_sock = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);

        if (client_sock >= 0) {
            printf("Client Connected\n");

            char *msg = "Connected to RFID Server\n";
            send(client_sock, msg, strlen(msg), 0);
        }
    }
}

// ---------------- MAIN ----------------
void app_main() {

    wifi_init();
    xTaskCreate(wifi_server_task, "server", 4096, NULL, 5, NULL);

    rc522_init();

    uint8_t uid[5];
    uint8_t last_uid[4] = {0};

    while (1) {

        if (rc522_detect()) {

            if (rc522_read_uid(uid)) {

                if (memcmp(uid, last_uid, 4) != 0) {

                    memcpy(last_uid, uid, 4);

                    const char* name = get_name(uid);

                    printf("UID: ");
                    for (int i = 0; i < 4; i++) {
                        printf("%02X ", uid[i]);
                    }

                    printf("-> %s\n", name);

                    char buffer[64];
                    sprintf(buffer, "Scanned: %s\n", name);

                    if (client_sock != -1) {
                        send(client_sock, buffer, strlen(buffer), 0);
                    }
                }
            }
        } else {
            memset(last_uid, 0, 4);
        }

        vTaskDelay(pdMS_TO_TICKS(300));
    }
}