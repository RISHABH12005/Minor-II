#include <stdio.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ---------------- PIN CONFIG ----------------
#define PIN_NUM_MISO 2    // J1 Pin 3
#define PIN_NUM_MOSI 7    // J1 Pin 8
#define PIN_NUM_CLK  6    // J1 Pin 7
#define PIN_NUM_CS   10   // J1 Pin 11
#define PIN_NUM_RST  4    // J3 Pin 8

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
        .clock_speed_hz = 500000,   // stable speed
        .mode = 0,
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 1,
    };

    spi_bus_add_device(SPI2_HOST, &devcfg, &spi);

    rc522_reset();

    rc522_write(ModeReg, 0x3D);
    rc522_antenna_on();

    printf("RC522 INIT DONE\n");

    // Debug antenna
    printf("TxControlReg: %02X\n", rc522_read(TxControlReg));
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

// ---------------- MAIN ----------------
void app_main() {

    rc522_init();

    uint8_t uid[5];

    while (1) {

        printf("Scanning...\n");

        if (rc522_detect()) {

            printf("CARD DETECTED\n");

            if (rc522_read_uid(uid)) {
                printf("UID: ");
                for (int i = 0; i < 4; i++) {
                    printf("%02X ", uid[i]);
                }
                printf("\n");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}