#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/nrfx_errors.h>
#include <nrfx_rtc.h>
#include <nrfx_gpiote.h>
#include <helpers/nrfx_gppi.h>
#include <hal/nrf_rtc.h>
#include <hal/nrf_gpio.h>

#include "inversion.h"

#ifndef CONFIG_BOARD_NATIVE_SIM


LOG_MODULE_REGISTER(inversion, LOG_LEVEL_INF);

static nrfx_gpiote_t gpiote_instance = NRFX_GPIOTE_INSTANCE(0);
static nrfx_rtc_t rtc2 = NRFX_RTC_INSTANCE(2);

#define EXTCOMIN_PIN NRF_GPIO_PIN_MAP(1, 12) // P1.12 en la Xiao BLE

void init_hardware_vcom(void){
    int err;

    //GPIOTE
    /* Pedimos a Zephyr un canal GPIOTE libre para no pisar la configuración de los botones */
    uint8_t gpiote_channel;
    err = nrfx_gpiote_channel_alloc(&gpiote_instance, &gpiote_channel);
    if (err != NRFX_SUCCESS) {
		LOG_ERR("Error: No GPIOTE channel available.\n");
        return;
    }
	
	/* Configuramos el pin en modo Task para que cambie de estado (Toggle) */
    nrfx_gpiote_output_config_t output_config = {
        .drive = NRF_GPIO_PIN_S0S1,
        .input_connect = NRF_GPIO_PIN_INPUT_DISCONNECT,
        .pull = NRF_GPIO_PIN_NOPULL,
    };

    nrfx_gpiote_task_config_t task_config = {
        .task_ch = gpiote_channel,
        .polarity = NRF_GPIOTE_POLARITY_TOGGLE,
        .init_val = NRF_GPIOTE_INITIAL_VALUE_LOW,
    };

    /* Inicializamos el pin y activamos la tarea */
    err = nrfx_gpiote_output_configure(&gpiote_instance, EXTCOMIN_PIN, &output_config, &task_config);
    if (err != NRFX_SUCCESS) {
        LOG_ERR("Error configuring GPIOTE output pin.\n");
        return;
    }

    nrfx_gpiote_out_task_enable(&gpiote_instance, EXTCOMIN_PIN);


    //RTC
    nrfx_rtc_config_t rtc_config = NRFX_RTC_DEFAULT_CONFIG;
    rtc_config.prescaler = 0; //el preescaler lo dejo asi por ejemplo
    
    err = nrfx_rtc_init(&rtc2, &rtc_config, NULL); //NULL para el handler
    if (err != NRFX_SUCCESS) {
        LOG_ERR("Error initializing RTC2: %d\n", err);
        return;
    }

    /* Configuramos el canal de comparación 0. 
       Valor 32768 = salta cada 1 segundo (1Hz de inversión, suficiente para evitar quemado). 
       El último parámetro 'false' para que no se genere una interrupción por software. */
    nrfx_rtc_cc_set(&rtc2, 0, 32768, false);


    //GPPI
    nrfx_gppi_handle_t ppi_toggle_handle;
    nrfx_gppi_handle_t ppi_clear_handle;


    /* Obtenemos las direcciones de memoria */
    uint32_t rtc_event_addr = nrfx_rtc_event_address_get(&rtc2, NRF_RTC_EVENT_COMPARE_0);
    uint32_t gpiote_task_addr = nrfx_gpiote_out_task_address_get(&gpiote_instance, EXTCOMIN_PIN);
    uint32_t rtc_task_clear_addr = nrfx_rtc_task_address_get(&rtc2, NRF_RTC_TASK_CLEAR);

    // first connect RTC Event -> GPIOTE Toggle Task
    err = nrfx_gppi_conn_alloc(rtc_event_addr, gpiote_task_addr, &ppi_toggle_handle);
    if (err != 0) {
        LOG_ERR("Failed to allocate GPPI toggle connection to GPIOTE\n");
        return;
    }

    // then connect RTC Event -> RTC Clear Task (To loop back to 0)
    err = nrfx_gppi_conn_alloc(rtc_event_addr, rtc_task_clear_addr, &ppi_clear_handle);
    if (err != 0) {
        LOG_ERR("Failed to allocate GPPI clear connection to RTC\n");
        return;
    }

    // Habilitamos las conexiones
    nrfx_gppi_conn_enable(ppi_toggle_handle);
    nrfx_gppi_conn_enable(ppi_clear_handle);
    
    //EMPEZAMOS
    nrfx_rtc_enable(&rtc2);
    
    LOG_INF("Hardware VCOM inversion initialized.\n");
}

#else

void init_hardware_vcom(void) {
    //Dummy que no haga nada
}

#endif