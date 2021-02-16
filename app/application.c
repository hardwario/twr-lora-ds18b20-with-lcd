// Tower Kit documentation https://tower.hardwario.com/
// SDK API description https://sdk.hardwario.com/
// Forum https://forum.hardwario.com/

#include <application.h>
#include <at.h>

#define DS18B20_SENSOR_COUNT 1
#define MEASURE_INTERVAL 1000

BC_DATA_STREAM_FLOAT_BUFFER(sm_temperature_buffer_0, 1)

bc_gfx_t *gfx;

bc_data_stream_t sm_temperature_0;

bc_data_stream_t *sm_temperature[] =
{
    &sm_temperature_0,
};

BC_DATA_STREAM_FLOAT_BUFFER(sm_voltage_buffer, 1)

bc_data_stream_t sm_voltage;

// LED instance
bc_led_t led;
bc_led_t lcd_led;
// Button instance
bc_button_t button;
// Lora instance
bc_cmwx1zzabz_t lora;
// ds18b20 library instance
static bc_ds18b20_t ds18b20;

float ds18b20_temperature = NAN;

bool active_state = false;

bc_scheduler_task_id_t battery_measure_task_id;
bc_scheduler_task_id_t ds18b20_measure_task_id;

bc_scheduler_task_id_t sleep_mode_task_id;

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
    if (!bc_module_battery_measure())
    {
        bc_scheduler_plan_current_now();
    }
}

void ds18b20_measure_task(void *param)
{
    if(!bc_ds18b20_measure(&ds18b20))
    {
        bc_scheduler_plan_current_now();
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
    bc_ds18b20_set_update_interval(&ds18b20, BC_TICK_INFINITY);
    bc_module_lcd_off();
}

void lcd_print_data(void)
{
    if (!bc_gfx_display_is_ready(gfx))
    {
        return;
    }

    bc_system_pll_enable();

    bc_gfx_clear(gfx);
    bc_gfx_update(gfx);

    bc_gfx_set_font(gfx, &bc_font_ubuntu_28);
    bc_gfx_draw_line(gfx, 0, 15, 128, 15, 1);

    int x = bc_gfx_printf(gfx, 20, 52, 1, "%.1f   ", ds18b20_temperature);

    bc_module_lcd_set_font(&bc_font_ubuntu_24);
    bc_gfx_draw_string(gfx, x - 20, 52, "\xb0 " "C   ", 1);

    bc_gfx_draw_line(gfx, 0, 113, 128, 113, 1);

    bc_gfx_update(gfx);
    bc_module_lcd_on();

    bc_system_pll_disable();
}

void battery_event_handler(bc_module_battery_event_t event, void *event_param)
{
    if (event == BC_MODULE_BATTERY_EVENT_UPDATE)
    {
        float voltage = NAN;
        bc_module_battery_get_voltage(&voltage);

        bc_data_stream_feed(&sm_voltage, &voltage);
    }
}

void button_event_handler(bc_button_t *self, bc_button_event_t event, void *event_param)
{
    (void) event_param;

    if (event == BC_BUTTON_EVENT_CLICK)
    {
        header = HEADER_BUTTON_CLICK;
    }
    else if (event == BC_BUTTON_EVENT_HOLD)
    {
        header = HEADER_BUTTON_HOLD;
    }
}

// Handler for the LCD
void lcd_event_handler(bc_module_lcd_event_t event, void *event_param)
{
    if(event == BC_MODULE_LCD_EVENT_LEFT_PRESS || event == BC_MODULE_LCD_EVENT_RIGHT_PRESS)
    {
        // Pulse the LED to show that the press was registered
        bc_led_pulse(&lcd_led, 500);
        bc_scheduler_plan_now(battery_measure_task_id);

        if(active_state == false)
        {
            // Set update for every 1 second
            bc_ds18b20_set_update_interval(&ds18b20, MEASURE_INTERVAL);
            bc_scheduler_plan_now(ds18b20_measure_task_id);
            active_state = true;
        }
        else
        {
            send_lora_packet();
            // Unregister task because it will be registered back
            bc_scheduler_unregister(sleep_mode_task_id);
        }

        // Plan the sleep mode 30 seconds from now
        sleep_mode_task_id = bc_scheduler_register(switch_to_sleep_mode, NULL, bc_tick_get() + 30000);
    }
}

void ds18b20_event_handler(bc_ds18b20_t *self, uint64_t device_address, bc_ds18b20_event_t event, void *event_param)
{
    (void) event_param;

    ds18b20_temperature = NAN;

    //bc_log_debug("EVENT: %d", event);

    if (event == BC_DS18B20_EVENT_UPDATE)
    {
        float value = NAN;

        bc_ds18b20_get_temperature_celsius(self, device_address, &value);
        ds18b20_temperature = value;

        // Print the data onto the LCD
        if(active_state == true)
        {
            lcd_print_data();
            bc_data_stream_feed(sm_temperature[0], &value);
        }
    }
}

// Send LoRa packet
void send_lora_packet(void)
{
    if (!bc_cmwx1zzabz_is_ready(&lora))
    {
        bc_scheduler_plan_current_relative(100);

        return;
    }

    static uint8_t buffer[4];
    size_t len = 0;

    buffer[len++] = header;

    float voltage_avg = NAN;

    bc_data_stream_get_average(&sm_voltage, &voltage_avg);

    buffer[len++] = !isnan(voltage_avg) ? ceil(voltage_avg * 10.f) : 0xff;

    float temperature_avg = NAN;

    bc_data_stream_get_average(sm_temperature[0], &temperature_avg);

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

    bc_cmwx1zzabz_send_message(&lora, buffer, len);

    static char tmp[sizeof(buffer) * 2 + 1];

    for (size_t i = 0; i < len; i++)
    {
        sprintf(tmp + i * 2, "%02x", buffer[i]);
    }

    bc_atci_printf("$SEND: %s", tmp);
    bc_led_pulse(&lcd_led, 500);

    header = HEADER_UPDATE;
}

bool at_send(void)
{
    bc_scheduler_plan_now(0);

    return true;
}

bool at_status(void)
{
    float value_avg = NAN;

    if (bc_data_stream_get_average(&sm_voltage, &value_avg))
    {
        bc_atci_printf("$STATUS: \"Voltage\",%.1f", value_avg);
    }
    else
    {
        bc_atci_printf("$STATUS: \"Voltage\",");
    }

    int sensor_found = bc_ds18b20_get_sensor_found(&ds18b20);

    for (int i = 0; i < sensor_found; i++)
    {
        value_avg = NAN;

        if (bc_data_stream_get_average(sm_temperature[i], &value_avg))
        {
            bc_atci_printf("$STATUS: \"Temperature%d\",%.1f", i, value_avg);
        }
        else
        {
            bc_atci_printf("$STATUS: \"Temperature%d\",", i);
        }
    }

    return true;
}

void lora_callback(bc_cmwx1zzabz_t *self, bc_cmwx1zzabz_event_t event, void *event_param)
{
    if (event == BC_CMWX1ZZABZ_EVENT_ERROR)
    {
        bc_led_set_mode(&led, BC_LED_MODE_BLINK_FAST);
    }
    else if (event == BC_CMWX1ZZABZ_EVENT_SEND_MESSAGE_START)
    {
        bc_led_blink(&led, 5);

        bc_scheduler_plan_relative(battery_measure_task_id, 20);
    }
    else if (event == BC_CMWX1ZZABZ_EVENT_SEND_MESSAGE_DONE)
    {
        bc_led_set_mode(&led, BC_LED_MODE_OFF);
    }
    else if (event == BC_CMWX1ZZABZ_EVENT_READY)
    {
        bc_led_set_mode(&led, BC_LED_MODE_OFF);
    }
    else if (event == BC_CMWX1ZZABZ_EVENT_JOIN_SUCCESS)
    {
        bc_atci_printf("$JOIN_OK");
    }
    else if (event == BC_CMWX1ZZABZ_EVENT_JOIN_ERROR)
    {
        bc_atci_printf("$JOIN_ERROR");
    }
}

// Application initialization function which is called once after boot
void application_init(void)
{
    //bc_log_init(BC_LOG_LEVEL_DEBUG, BC_LOG_TIMESTAMP_ABS);
    // Initialize 1-Wire temperature sensors
    bc_ds18b20_init_single(&ds18b20, BC_DS18B20_RESOLUTION_BITS_12);
    bc_ds18b20_set_event_handler(&ds18b20, ds18b20_event_handler, NULL);

    ds18b20_measure_task_id = bc_scheduler_register(ds18b20_measure_task, NULL, BC_TICK_INFINITY);
    
    // Initialize LED
    bc_led_init(&led, BC_GPIO_LED, false, false);
    bc_led_set_mode(&led, BC_LED_MODE_OFF);

    // Init LCD and LCD LEDs
    const bc_led_driver_t* driver = bc_module_lcd_get_led_driver();
    bc_led_init_virtual(&lcd_led, BC_MODULE_LCD_LED_GREEN, driver, 1);

    bc_module_lcd_init();
    bc_module_lcd_set_event_handler(lcd_event_handler, NULL);
    gfx = bc_module_lcd_get_gfx();

    // Turn off the LCD for Low power consumption
    bc_module_lcd_off();

    bc_led_pulse(&led, 2000);

    // Initialize battery
    bc_module_battery_init();
    bc_module_battery_set_event_handler(battery_event_handler, NULL);
    battery_measure_task_id = bc_scheduler_register(battery_measure_task, NULL, 1000);


    // Init stream buffers for averaging
    bc_data_stream_init(&sm_voltage, 1, &sm_voltage_buffer);
    bc_data_stream_init(&sm_temperature_0, 1, &sm_temperature_buffer_0);

    // Initialize lora module
    bc_cmwx1zzabz_init(&lora, BC_UART_UART1);
    bc_cmwx1zzabz_set_event_handler(&lora, lora_callback, NULL);
    bc_cmwx1zzabz_set_mode(&lora, BC_CMWX1ZZABZ_CONFIG_MODE_ABP);
    bc_cmwx1zzabz_set_class(&lora, BC_CMWX1ZZABZ_CONFIG_CLASS_A);

    // Initialize AT command interface
    at_init(&led, &lora);
    static const bc_atci_command_t commands[] = {
            AT_LORA_COMMANDS,
            {"$SEND", at_send, NULL, NULL, NULL, "Immediately send packet"},
            {"$STATUS", at_status, NULL, NULL, NULL, "Show status"},
            AT_LED_COMMANDS,
            BC_ATCI_COMMAND_CLAC,
            BC_ATCI_COMMAND_HELP
    };
    bc_atci_init(commands, BC_ATCI_COMMANDS_LENGTH(commands));
}
