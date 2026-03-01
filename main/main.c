/*
 * SPDX-FileCopyrightText: 2022-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "class/hid/hid_device.h"
#include "driver/gpio.h"
#include "driver/uart.h"

#define APP_BUTTON (GPIO_NUM_0) // Use BOOT signal by default
static const char *TAG = "HIDcon-Device";

#define REPORT_LEN 11
static bool usb_ready = false;

/************* TinyUSB descriptors ****************/

#define TUSB_DESC_TOTAL_LEN      (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)

/**
 * @brief HID report descriptor
 *
 * In this example we implement Keyboard + Mouse HID device,
 * so we must define both report descriptors
 */
const uint8_t hid_report_descriptor[] = {
    0x05, 0x0D, // Usage Page (Digitizer)
    0x09, 0x04, // Usage (Touch Screen)
    0xA1, 0x01, // Collection (Application)
    // Finger 1
    0x09, 0x22, //   Usage (Finger)
    0xA1, 0x02, //   Collection (Logical)
    0x09, 0x42, //     Usage (Tip Switch)
    0x15, 0x00, //     Logical Minimum (0)
    0x25, 0x10, //     Logical Maximum (1)
    0x75, 0x01, //     Report Size (1)
    0x95, 0x01, //     Report Count (1)
    0x81, 0x02, //     Input (Data,Var,Abs)
    0x95, 0x07, //     Report Count (7) - padding to full byte
    0x81, 0x03, //     Input (Const,Var,Abs)
    0x09, 0x51, //     Usage (Contact Identifier)
    0x75, 0x08, //     Report Size (8)
    0x95, 0x01, //     Report Count (1)
    0x81, 0x02, //     Input (Data,Var,Abs)
    0x05, 0x01, //     Usage Page (Generic Desktop)
    // X
    0x09, 0x30,                             // Usage (X)
    0x15, 0x00,                             // Logical Minimum (0)
    0x27, /*41 ->*/ 0xfe, 0xff, 0xff, 0x7f, // Logical Maximum (0x7ffffffe)
    0x75, 0x20,                             // Report Size (32)
    0x95, 0x01,                             // Report Count (1)
    0x81, 0x02,                             // Input (Data,Var,Abs)
    // Y
    0x09, 0x31,                             // Usage (Y)
    0x15, 0x00,                             // Logical Minimum (0)
    0x27, /*56 ->*/ 0xfe, 0xff, 0xff, 0x7f, // Logical Maximum (0x7ffffffe)
    0x75, 0x20,                             // Report Size (32)
    0x95, 0x01,
    0x81, 0x02,
    0xC0, //   End Collection
    // Contact Count
    0x05, 0x0D, // Usage Page (Digitizer)
    0x09, 0x54, // Usage (Contact Count)
    0x25, 0x02, // Logical Maximum (2)
    0x75, 0x08, // Report Size (8)
    0x95, 0x01, // Report Count (1)
    0x81, 0x02, // Input (Data,Var,Abs)
    0xC0        // End Collection (Application)
};

/**
 * @brief String descriptor
 */
const char* hid_string_descriptor[5] = {
    // array of pointer to string descriptors
    (char[]){0x09, 0x04},  // 0: is supported language is English (0x0409)
    "Sakura1",             // 1: Manufacturer
    "Sakura1 Device",      // 2: Product
    "000101",              // 3: Serials, should use chip ID
    "HID TouchScreen",  // 4: HID
};

/**
 * @brief Configuration descriptor
 *
 * This is a simple configuration descriptor that defines 1 configuration and 1 HID interface
 */
static const uint8_t hid_configuration_descriptor[] = {
    // Configuration number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface number, string index, boot protocol, report descriptor len, EP In address, size & polling interval
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(hid_report_descriptor), 0x81, 64, 1),
};

/********* TinyUSB HID callbacks ***************/

// Invoked when received GET HID REPORT DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    // We use only one interface and one HID report descriptor, so we can ignore parameter 'instance'
    return hid_report_descriptor;
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;

    return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
{
}

void my_tinyusb_event_cb(tinyusb_event_t *event, void *arg)
{
    switch (event->id) {
    case TINYUSB_EVENT_ATTACHED:
        ESP_LOGI(TAG, "USB connect");
        usb_ready = true;
        break;
    case TINYUSB_EVENT_DETACHED:
        ESP_LOGI(TAG, "USB disconnect");
        usb_ready = false;
        break;
    default:
        break;
    }
}
/********* Application ***************/

/* 发送 11 B 触摸报文 */
bool ts_send_report(const uint8_t *data)
{
    // return tud_hid_report(0, data, REPORT_LEN);

    // if (usb_ready && tud_hid_ready()) {
    //     return tud_hid_report(0, data, REPORT_LEN);
    // }
    // return false;

    while(usb_ready && tud_hid_ready()){
    }
    return tud_hid_report(0, data, REPORT_LEN);
}

bool ts_send_report_blocking(const uint8_t *data, uint32_t timeout_ms)
{
    uint32_t start = xTaskGetTickCount();
    uint32_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    
    while (usb_ready) {
        if (tud_hid_ready()) {
            return tud_hid_report(0, data, REPORT_LEN);
        }
        // 短暂让步，避免忙等饿死其他任务
        esp_rom_delay_us(1);
        
        // 超时检查
        if (xTaskGetTickCount() - start > timeout_ticks) {
            break;
        }
    }
    return false;
}

/* 串口收包任务 */
static void uart_rx_task(void *arg)
{
    //======================初始化时候测试===================================
    //双指 从坐上到右下，从右上到坐下，划线
    // for (int i = 0 ; i< 0x7ffffffe ; i++){
    //     int x_1 = i;
    //     int y_1 = i;
    //     int x_2 = 0x7ffffffe - i;
    //     int y_2 = i;
    //     ts_send_report((uint8_t[]){0x01,0x01,
    //         (uint8_t)(x_1 & 0xFF), (uint8_t)((x_1 >> 8) & 0xFF), (uint8_t)((x_1 >> 16) & 0xFF), (uint8_t)((x_1 >> 24) & 0xFF),
    //         (uint8_t)(y_1 & 0xFF), (uint8_t)((y_1 >> 8) & 0xFF), (uint8_t)((y_1 >> 16) & 0xFF), (uint8_t)((y_1 >> 24) & 0xFF),
    //     });
    //     ts_send_report((uint8_t[]){0x01,0x02,
    //         (uint8_t)(x_2 & 0xFF), (uint8_t)((x_2 >> 8) & 0xFF), (uint8_t)((x_2 >> 16) & 0xFF), (uint8_t)((x_2 >> 24) & 0xFF),
    //         (uint8_t)(y_2 & 0xFF), (uint8_t)((y_2 >> 8) & 0xFF), (uint8_t)((y_2 >> 16) & 0xFF), (uint8_t)((y_2 >> 24) & 0xFF),
    //     });
    // }
    // ts_send_report((uint8_t[]){0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00});
    // ts_send_report((uint8_t[]){0x00,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00});
    //======================测试结束===================================

    uint8_t head, payload[REPORT_LEN];
    while (1) {
        if (uart_read_bytes(UART_NUM_0, &head, 1, portMAX_DELAY) == 1) {
            if (head == 0xF4) {
                uart_read_bytes(UART_NUM_0, payload, REPORT_LEN, portMAX_DELAY);
                    ts_send_report(payload);
            }
        }
    }
}

void app_main(void)
{
    /* GPIO */
    // Initialize button that will trigger HID reports
    const gpio_config_t boot_button_config = {
        .pin_bit_mask = BIT64(APP_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_up_en = true,
        .pull_down_en = false,
    };
    ESP_ERROR_CHECK(gpio_config(&boot_button_config));

    /* UART */
    // ESP_LOGI(TAG, "UART initialization");
    uart_config_t uart_cfg = {
        .baud_rate = 2000000,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_param_config(UART_NUM_0, &uart_cfg);
    uart_driver_install(UART_NUM_0, 2048, 0, 0, NULL, 0);
    // uart_set_rx_full_threshold(UART_NUM_0, 60);
    // uart_set_rx_timeout(UART_NUM_0, 2);
    // ESP_LOGI(TAG, "UART initialization DONE");
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "HIDcon-Device v0.1.1-20260116");

    /* USB */
    ESP_LOGI(TAG, "USB initialization");
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    tusb_cfg.descriptor.device = NULL;
    tusb_cfg.descriptor.full_speed_config = hid_configuration_descriptor;
    tusb_cfg.descriptor.string = hid_string_descriptor;
    tusb_cfg.descriptor.string_count = sizeof(hid_string_descriptor) / sizeof(hid_string_descriptor[0]);
    tusb_cfg.event_cb = my_tinyusb_event_cb;//回调事件判断是否接入
#if (TUD_OPT_HIGH_SPEED)
    tusb_cfg.descriptor.high_speed_config = hid_configuration_descriptor;
#endif // TUD_OPT_HIGH_SPEED
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(TAG, "USB initialization DONE");
    // xTaskCreate(uart_rx_task, "uart_rx", 2048, NULL, 5, NULL);
    xTaskCreatePinnedToCore(uart_rx_task, "uart_rx", 4096, NULL, 10, NULL, 1);
}