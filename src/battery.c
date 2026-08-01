#include "battery.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(battery, LOG_LEVEL_INF);

/* Nodos del Device Tree */
#define VBATT_NODE DT_NODELABEL(vbatt)

static const struct adc_dt_spec vbatt_adc = ADC_DT_SPEC_GET(VBATT_NODE);
static const struct gpio_dt_spec vbatt_power = GPIO_DT_SPEC_GET(VBATT_NODE, power_gpios);

#define FULL_OHMS DT_PROP(VBATT_NODE, full_ohms)
#define OUTPUT_OHMS DT_PROP(VBATT_NODE, output_ohms)
#define DELAY_US DT_PROP(VBATT_NODE, power_on_sample_delay_us)

//usa una cola de trabajo, por lo que no bloquea y no satura nada.
static struct k_work_delayable battery_work;

static struct app_state *m_app_state;

/* La función que se ejecuta cada 5 minutos en segundo plano */
static void battery_read_handler(struct k_work *work)
{
    int err;
    uint16_t buf;
    struct adc_sequence seq = {
        .buffer = &buf,
        .buffer_size = sizeof(buf),
    };

    adc_sequence_init_dt(&vbatt_adc, &seq);

    // Encendemos el divisor
    gpio_pin_set_dt(&vbatt_power, 1);
    k_busy_wait(DELAY_US);

    // Leemos el valor del ADC
    err = adc_read(vbatt_adc.dev, &seq);
    
    // Apagamos el divisor instantáneamente
    gpio_pin_set_dt(&vbatt_power, 0);

    if (err == 0) {
        int32_t val_mv = buf;
        
        adc_raw_to_millivolts_dt(&vbatt_adc, &val_mv);
        
        int32_t real_mv = val_mv * FULL_OHMS / OUTPUT_OHMS;
        
        // Formulita de empotrados (4.2V = 100%, 3.3V = 0%)
        int percent = (real_mv - 3300) * 100 / (4200 - 3300);
        
        if (percent > 100) percent = 100;
        if (percent < 0) percent = 0;

        // Actualizamos el estado global
        if (m_app_state) {
            app_set_battery_percent(m_app_state, percent);
        }
        
        LOG_INF("Battery level: %d%% (%d mV)", percent, real_mv);
    } else {
        LOG_ERR("Error reading ADC: %d", err);
    }

    // Reprogramar para dentro de 5 minutos
    k_work_reschedule(&battery_work, K_MINUTES(5));
}

/* Función de inicialización expuesta al main */
void battery_init(struct app_state *state)
{
    m_app_state = state;

    if (!gpio_is_ready_dt(&vbatt_power)) {
        LOG_ERR("Pin power_gpios isn't ready");
        return;
    }
    gpio_pin_configure_dt(&vbatt_power, GPIO_OUTPUT_INACTIVE);

    if (!adc_is_ready_dt(&vbatt_adc)) {
        LOG_ERR("ADC isn't ready");
        return;
    }
    adc_channel_setup_dt(&vbatt_adc);

    // Inicializamos el trabajo y lanzamos la primera lectura ya mismo
    k_work_init_delayable(&battery_work, battery_read_handler);
    k_work_schedule(&battery_work, K_NO_WAIT);
    
    LOG_INF("Battery module initialized.");
}