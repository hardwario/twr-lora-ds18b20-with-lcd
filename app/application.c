#include <application.h>
#include <bc_ds18b20.h>

/*

 SENSOR MODULE CONNECTION
==========================

Sensor Module R1.0 - 4 pin connector
VCC, GND, - , DATA

Sensor Module R1.1 - 5 pin connector
- , GND , VCC , - , DATA


 DS18B20 sensor pinout
=======================
VCC - red
GND - black
DATA- yellow (white)

*/

// Time after the sending is less frequent to save battery
#define SERVICE_INTERVAL_INTERVAL (10 * 60 * 1000)
#define BATTERY_UPDATE_INTERVAL   (30 * 60 * 1000)

#define UPDATE_SERVICE_INTERVAL            (5 * 1000)
#define UPDATE_NORMAL_INTERVAL             (1 * 60 * 1000)

#define TEMPERATURE_DS18B20_PUB_NO_CHANGE_INTEVAL (5  * 60* 1000)
#define TEMPERATURE_DS18B20_PUB_VALUE_CHANGE 0.5f

static bc_led_t led;

float temperature_on_display = NAN;

static bc_ds18b20_t ds18b20;

int device_index;
bc_gfx_t *gfx;

struct {
    event_param_t temperature;
    event_param_t temperature_ds18b20;

} params;

void handler_ds18b20(bc_ds18b20_t *s, uint64_t device_id, bc_ds18b20_event_t e, void *p);

void climate_module_event_handler(bc_module_climate_event_t event, void *event_param);

void switch_to_normal_mode_task(void *param);

void battery_event_handler(bc_module_battery_event_t e, void *p)
{
    (void) e;
    (void) p;

    float voltage;

    if (bc_module_battery_get_voltage(&voltage))
    {
        bc_radio_pub_battery(&voltage);
    }
}

void ds18b20_event_handler(bc_ds18b20_t *self, uint64_t device_address, bc_ds18b20_event_t e, void *p)
{
    (void) p;

    temperature_on_display = NAN;

    if (e == BC_DS18B20_EVENT_UPDATE)
    {
        float value = NAN;

        bc_ds18b20_get_temperature_celsius(self, device_address, &value);

        //bc_log_debug("UPDATE %" PRIx64 "(%d) = %f", device_address, device_index, value);

        if ((fabs(value - params.temperature_ds18b20.value) >= TEMPERATURE_DS18B20_PUB_VALUE_CHANGE) || (params.temperature_ds18b20.next_pub < bc_scheduler_get_spin_tick()))
        {
            static char topic[64];
            snprintf(topic, sizeof(topic), "thermometer/%" PRIx64 "/temperature", device_address);
            bc_radio_pub_float(topic, &value);
            params.temperature_ds18b20.value = value;
            params.temperature_ds18b20.next_pub = bc_scheduler_get_spin_tick() + TEMPERATURE_DS18B20_PUB_NO_CHANGE_INTEVAL;
        }

        temperature_on_display = value;
    }

    bc_scheduler_plan_now(0);
}

// This task is fired once after the SERVICE_INTERVAL_INTERVAL milliseconds and changes the period
// of measurement. After module power-up you get faster updates so you can test the module and see
// instant changes. After SERVICE_INTERVAL_INTERVAL the update period is longer to save batteries.
void switch_to_normal_mode_task(void *param)
{
    bc_ds18b20_set_update_interval(&ds18b20, UPDATE_NORMAL_INTERVAL);

    bc_scheduler_unregister(bc_scheduler_get_current_task_id());
}

void application_init(void)
{
    bc_led_init(&led, BC_GPIO_LED, false, false);
    bc_led_set_mode(&led, BC_LED_MODE_OFF);

    bc_radio_init(BC_RADIO_MODE_NODE_SLEEPING);

    bc_module_battery_init();
    bc_module_battery_set_event_handler(battery_event_handler, NULL);
    bc_module_battery_set_update_interval(BATTERY_UPDATE_INTERVAL);

    // For multiple sensor you can call bc_ds18b20_init() more in sdk/_excamples/ds18b20_multiple
    bc_ds18b20_init_single(&ds18b20, BC_DS18B20_RESOLUTION_BITS_12);

    bc_ds18b20_set_event_handler(&ds18b20, ds18b20_event_handler, NULL);
    bc_ds18b20_set_update_interval(&ds18b20, UPDATE_SERVICE_INTERVAL);

    bc_scheduler_register(switch_to_normal_mode_task, NULL, SERVICE_INTERVAL_INTERVAL);

    bc_radio_pairing_request("printer-temperature-monitor", VERSION);

    bc_led_pulse(&led, 2000);

    bc_module_lcd_init();
    gfx = bc_module_lcd_get_gfx();

    //bc_log_init(BC_LOG_LEVEL_DEBUG, BC_LOG_TIMESTAMP_ABS);
    bc_scheduler_plan_from_now(0, 1000);
}

void application_task(void)
{
    static char str_temperature[10];

    if (!bc_module_lcd_is_ready())
    {
    	return;
    }

    bc_system_pll_enable();

    bc_gfx_clear(gfx);
    bc_gfx_set_font(gfx, &bc_font_ubuntu_33);
    int x = bc_gfx_printf(gfx, 20,20, 1, "%.1f   ", temperature_on_display);

    bc_module_lcd_set_font(&bc_font_ubuntu_24);
    bc_gfx_draw_string(gfx, x - 20, 25, "\xb0" "C   ", 1);

    bc_gfx_update(gfx);

    bc_system_pll_disable();
}
