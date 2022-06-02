// Tower Kit documentation https://tower.hardwario.com/
// SDK API description https://sdk.hardwario.com/
// Forum https://forum.hardwario.com/

#include <application.h>
#include <at.h>

#define DS18B20_SENSOR_COUNT 1
#define MEASURE_INTERVAL 1000

TWR_DATA_STREAM_FLOAT_BUFFER(sm_temperature_buffer_0, 1)

twr_gfx_t *gfx;

twr_data_stream_t sm_temperature_0;

twr_data_stream_t *sm_temperature[] =
{
    &sm_temperature_0,
};

TWR_DATA_STREAM_FLOAT_BUFFER(sm_voltage_buffer, 1)

twr_data_stream_t sm_voltage;

// LED instance
twr_led_t led;
twr_led_t lcd_led;
// Button instance
twr_button_t button;
// Lora instance
twr_cmwx1zzabz_t lora;
// ds18b20 library instance
static twr_ds18b20_t ds18b20;

float ds18b20_temperature = NAN;

bool active_state = false;

twr_scheduler_task_id_t battery_measure_task_id;
twr_scheduler_task_id_t ds18b20_measure_task_id;

twr_scheduler_task_id_t sleep_mode_task_id;

enum {
    HEADER_BOOT         = 0x00,
    HEADER_UPDATE       = 0x01,
    HEADER_BUTTON_CLICK = 0x02,
    HEADER_BUTTON_HOLD  = 0x03,

} header = HEADER_BOOT;


void send_lora_packet(void);
void process_data();

void battery_measure_task(void *param)
{
    if (!twr_module_battery_measure())
    {
        twr_scheduler_plan_current_now();
    }
}

void ds18b20_measure_task(void *param)
{
    if(!twr_ds18b20_measure(&ds18b20))
    {
        twr_scheduler_plan_current_now();
    }
    else
    {
        send_lora_packet();
    }

}

// Turn of the LCD and go to the sleep mode
void switch_to_sleep_mode()
{
    active_state = false;
    twr_ds18b20_set_update_interval(&ds18b20, TWR_TICK_INFINITY);
    twr_module_lcd_off();
}

void lcd_print_data(void)
{
    if (!twr_gfx_display_is_ready(gfx))
    {
        return;
    }

    twr_system_pll_enable();

    twr_gfx_clear(gfx);
    twr_gfx_update(gfx);

    twr_gfx_set_font(gfx, &twr_font_ubuntu_28);
    twr_gfx_draw_line(gfx, 0, 15, 128, 15, 1);

    int x = twr_gfx_printf(gfx, 20, 52, 1, "%.1f   ", ds18b20_temperature);

    twr_module_lcd_set_font(&twr_font_ubuntu_24);
    twr_gfx_draw_string(gfx, x - 20, 52, "\xb0 " "C   ", 1);

    twr_gfx_draw_line(gfx, 0, 113, 128, 113, 1);

    twr_gfx_update(gfx);
    twr_module_lcd_on();

    twr_system_pll_disable();
}

void battery_event_handler(twr_module_battery_event_t event, void *event_param)
{
    if (event == TWR_MODULE_BATTERY_EVENT_UPDATE)
    {
        float voltage = NAN;
        twr_module_battery_get_voltage(&voltage);

        twr_data_stream_feed(&sm_voltage, &voltage);
    }
}

void button_event_handler(twr_button_t *self, twr_button_event_t event, void *event_param)
{
    (void) event_param;

    if (event == TWR_BUTTON_EVENT_CLICK)
    {
        header = HEADER_BUTTON_CLICK;
    }
    else if (event == TWR_BUTTON_EVENT_HOLD)
    {
        header = HEADER_BUTTON_HOLD;
    }
}

// Handler for the LCD
void lcd_event_handler(twr_module_lcd_event_t event, void *event_param)
{
    if(event == TWR_MODULE_LCD_EVENT_LEFT_PRESS || event == TWR_MODULE_LCD_EVENT_RIGHT_PRESS)
    {
        // Pulse the LED to show that the press was registered
        twr_led_pulse(&lcd_led, 500);
        twr_scheduler_plan_now(battery_measure_task_id);

        if(active_state == false)
        {
            // Set update for every 1 second
            twr_ds18b20_set_update_interval(&ds18b20, MEASURE_INTERVAL);
            twr_scheduler_plan_now(ds18b20_measure_task_id);
            active_state = true;
        }
        else
        {
            send_lora_packet();
            // Unregister task because it will be registered back
            twr_scheduler_unregister(sleep_mode_task_id);
        }

        // Plan the sleep mode 30 seconds from now
        sleep_mode_task_id = twr_scheduler_register(switch_to_sleep_mode, NULL, twr_tick_get() + 30000);
    }
}

void ds18b20_event_handler(twr_ds18b20_t *self, uint64_t device_address, twr_ds18b20_event_t event, void *event_param)
{
    (void) event_param;

    ds18b20_temperature = NAN;

    //twr_log_debug("EVENT: %d", event);

    if (event == TWR_DS18B20_EVENT_UPDATE)
    {
        float value = NAN;

        twr_ds18b20_get_temperature_celsius(self, device_address, &value);
        ds18b20_temperature = value;

        // Print the data onto the LCD
        if(active_state == true)
        {
            lcd_print_data();
            twr_data_stream_feed(sm_temperature[0], &value);
        }
    }
}

// Send LoRa packet
void send_lora_packet(void)
{
    if (!twr_cmwx1zzabz_is_ready(&lora))
    {
        twr_scheduler_plan_current_relative(100);

        return;
    }

    static uint8_t buffer[4];
    size_t len = 0;

    buffer[len++] = header;

    float voltage_avg = NAN;

    twr_data_stream_get_average(&sm_voltage, &voltage_avg);

    buffer[len++] = !isnan(voltage_avg) ? ceil(voltage_avg * 10.f) : 0xff;

    float temperature_avg = NAN;

    twr_data_stream_get_average(sm_temperature[0], &temperature_avg);

    if (!isnan(temperature_avg))
    {
        int16_t temperature_i16 = (int16_t) (temperature_avg * 10.f);

        buffer[len++] = temperature_i16 >> 8;
        buffer[len++] = temperature_i16;
    }
    else
    {
        buffer[len++] = 0xff;
        buffer[len++] = 0xff;
    }

    twr_cmwx1zzabz_send_message(&lora, buffer, len);

    static char tmp[sizeof(buffer) * 2 + 1];

    for (size_t i = 0; i < len; i++)
    {
        sprintf(tmp + i * 2, "%02x", buffer[i]);
    }

    twr_atci_printf("$SEND: %s", tmp);
    twr_led_pulse(&lcd_led, 500);

    header = HEADER_UPDATE;
}

bool at_send(void)
{
    twr_scheduler_plan_now(0);

    return true;
}

bool at_status(void)
{
    float value_avg = NAN;

    if (twr_data_stream_get_average(&sm_voltage, &value_avg))
    {
        twr_atci_printf("$STATUS: \"Voltage\",%.1f", value_avg);
    }
    else
    {
        twr_atci_printf("$STATUS: \"Voltage\",");
    }

    int sensor_found = twr_ds18b20_get_sensor_found(&ds18b20);

    for (int i = 0; i < sensor_found; i++)
    {
        value_avg = NAN;

        if (twr_data_stream_get_average(sm_temperature[i], &value_avg))
        {
            twr_atci_printf("$STATUS: \"Temperature%d\",%.1f", i, value_avg);
        }
        else
        {
            twr_atci_printf("$STATUS: \"Temperature%d\",", i);
        }
    }

    return true;
}

void lora_callback(twr_cmwx1zzabz_t *self, twr_cmwx1zzabz_event_t event, void *event_param)
{
    if (event == TWR_CMWX1ZZABZ_EVENT_ERROR)
    {
        twr_led_set_mode(&led, TWR_LED_MODE_BLINK_FAST);
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_SEND_MESSAGE_START)
    {
        twr_led_blink(&led, 5);

        twr_scheduler_plan_relative(battery_measure_task_id, 20);
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_SEND_MESSAGE_DONE)
    {
        twr_led_set_mode(&led, TWR_LED_MODE_OFF);
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_READY)
    {
        twr_led_set_mode(&led, TWR_LED_MODE_OFF);
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_JOIN_SUCCESS)
    {
        twr_atci_printf("$JOIN_OK");
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_JOIN_ERROR)
    {
        twr_atci_printf("$JOIN_ERROR");
    }
}

// Application initialization function which is called once after boot
void application_init(void)
{
    twr_log_init(TWR_LOG_LEVEL_DEBUG, TWR_LOG_TIMESTAMP_ABS);
    // Initialize 1-Wire temperature sensors
    twr_ds18b20_init_single(&ds18b20, TWR_DS18B20_RESOLUTION_BITS_12);
    twr_ds18b20_set_event_handler(&ds18b20, ds18b20_event_handler, NULL);

    ds18b20_measure_task_id = twr_scheduler_register(ds18b20_measure_task, NULL, TWR_TICK_INFINITY);

    // Initialize LED
    twr_led_init(&led, TWR_GPIO_LED, false, false);
    twr_led_set_mode(&led, TWR_LED_MODE_OFF);

    // Init LCD and LCD LEDs
    const twr_led_driver_t* driver = twr_module_lcd_get_led_driver();
    twr_led_init_virtual(&lcd_led, TWR_MODULE_LCD_LED_GREEN, driver, 1);

    twr_module_lcd_init();
    twr_module_lcd_set_event_handler(lcd_event_handler, NULL);
    gfx = twr_module_lcd_get_gfx();

    // Turn off the LCD for Low power consumption
    twr_module_lcd_off();

    twr_led_pulse(&led, 2000);

    // Initialize battery
    twr_module_battery_init();
    twr_module_battery_set_event_handler(battery_event_handler, NULL);
    battery_measure_task_id = twr_scheduler_register(battery_measure_task, NULL, 1000);

    // Init stream buffers for averaging
    twr_data_stream_init(&sm_voltage, 1, &sm_voltage_buffer);
    twr_data_stream_init(&sm_temperature_0, 1, &sm_temperature_buffer_0);

    // Initialize lora module
    twr_cmwx1zzabz_init(&lora, TWR_UART_UART1);
    twr_cmwx1zzabz_set_event_handler(&lora, lora_callback, NULL);
    twr_cmwx1zzabz_set_mode(&lora, TWR_CMWX1ZZABZ_CONFIG_MODE_ABP);
    twr_cmwx1zzabz_set_class(&lora, TWR_CMWX1ZZABZ_CONFIG_CLASS_A);

    // Initialize AT command interface
    at_init(&led, &lora);
    static const twr_atci_command_t commands[] = {
            AT_LORA_COMMANDS,
            {"$SEND", at_send, NULL, NULL, NULL, "Immediately send packet"},
            {"$STATUS", at_status, NULL, NULL, NULL, "Show status"},
            AT_LED_COMMANDS,
            TWR_ATCI_COMMAND_CLAC,
            TWR_ATCI_COMMAND_HELP
    };
    twr_atci_init(commands, TWR_ATCI_COMMANDS_LENGTH(commands));
}
