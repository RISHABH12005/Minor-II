#include <stdio.h>
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "MAX30102"

// 🔥 Use safer pins for ESP32-C5
#define SDA_GPIO 6
#define SCL_GPIO 7
#define POWER_GPIO 5

#define I2C_PORT 0
#define I2C_FREQ_HZ 50000   // 🔥 lower speed for stability

#define MAX30102_ADDR 0x57

i2c_master_bus_handle_t bus_handle;
i2c_master_dev_handle_t dev_handle;

// 🔌 Power control
void power_on_sensor() {
    gpio_set_level(POWER_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(500));  // 🔥 important delay
}

void power_off_sensor() {
    gpio_set_level(POWER_GPIO, 0);
}

// ⚙️ GPIO init
void init_gpio() {
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << POWER_GPIO)
    };
    gpio_config(&io_conf);
}

// ⚙️ I2C init (NEW DRIVER)
void init_i2c() {
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_PORT,
        .sda_io_num = SDA_GPIO,
        .scl_io_num = SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true   // 🔥 CRITICAL FIX
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));

    i2c_device_config_t dev_config = {
        .device_address = MAX30102_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };

    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_config, &dev_handle));
}

// 📡 Write register
void write_register(uint8_t reg, uint8_t value) {
    uint8_t data[2] = {reg, value};

    esp_err_t ret = i2c_master_transmit(dev_handle, data, 2, 1000 / portTICK_PERIOD_MS);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Write failed: %s", esp_err_to_name(ret));
    }
}

// 📡 Read register
uint8_t read_register(uint8_t reg) {
    uint8_t data = 0;

    esp_err_t ret = i2c_master_transmit_receive(
        dev_handle,
        &reg, 1,
        &data, 1,
        1000 / portTICK_PERIOD_MS
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Read failed: %s", esp_err_to_name(ret));
        return 0;
    }

    return data;
}

// 🔍 Check sensor presence
void check_sensor() {
    uint8_t part_id = read_register(0xFF);

    if (part_id == 0x15 || part_id == 0x11) {
        ESP_LOGI(TAG, "MAX30102 detected ✅ (PART ID: 0x%02X)", part_id);
    } else {
        ESP_LOGE(TAG, "Sensor not detected ❌ (PART ID: 0x%02X)", part_id);
    }
}

// ⚙️ Initialize sensor
void init_max30102() {
    ESP_LOGI(TAG, "Initializing sensor...");

    write_register(0x09, 0x40);  // Reset
    vTaskDelay(pdMS_TO_TICKS(100));

    write_register(0x09, 0x03);  // SpO2 mode
    write_register(0x0A, 0x27);  // Sample config
    write_register(0x0C, 0x24);  // LED current
}

// 🚀 Main
void app_main(void) {

    init_gpio();

    ESP_LOGI(TAG, "Power OFF");
    power_off_sensor();
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI(TAG, "Power ON");
    power_on_sensor();

    init_i2c();

    check_sensor();   // 🔍 detect

    init_max30102();  // ⚙️ configure

    while (1) {
        uint8_t status = read_register(0x00);

        ESP_LOGI(TAG, "Status: 0x%02X", status);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}