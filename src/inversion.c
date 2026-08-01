#include <zephyr/kernel.h>
#include <nrfx_rtc.h>
#include <nrfx_gpiote.h>
#include <nrfx_ppi.h>
#include <hal/nrf_gpio.h>
#include <zephyr/logging/log.h>


#include "inversion.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static const nrfx_rtc_t rtc2 = NRFX_RTC_INSTANCE(2);

#define EXTCOMIN_PIN NRF_GPIO_PIN_MAP(1, 12) // P1.12 en la Xiao BLE

void init_hardware_vcom(void)
{
    nrfx_err_t err;

	//GPIOTE
    /* Configuramos el pin en modo Task para que cambie de estado (Toggle) */
    nrfx_gpiote_out_config_t gpiote_config = NRFX_GPIOTE_CONFIG_OUT_TASK_TOGGLE(false);
    
    /* Pedimos a Zephyr un canal GPIOTE libre para no pisar la configuración de los botones */
    uint8_t gpiote_channel;
    err = nrfx_gpiote_channel_alloc(&gpiote_channel);
    if (err != NRFX_SUCCESS) {
        LOG_ERR("Error: No GPIOTE channel available.\n");
        return;
    }

    /* Inicializamos el pin y activamos la tarea */
    nrfx_gpiote_out_init(EXTCOMIN_PIN, &gpiote_config);
    nrfx_gpiote_out_task_enable(EXTCOMIN_PIN);


	//RTC
    nrfx_rtc_config_t rtc_config = NRFX_RTC_DEFAULT_CONFIG;
    rtc_config.prescaler = 0; //el preescaler lo dejo asi por ejemplo
    
    err = nrfx_rtc_init(&rtc2, &rtc_config, NULL); //NULL para el handler

    /* Configuramos el canal de comparación 0. 
       Valor 32768 = salta cada 1 segundo (1Hz de inversión, suficiente para evitar quemado). 
       El último parámetro 'false' evita que se genere una interrupción por software. */
    nrfx_rtc_cc_set(&rtc2, 0, 32768, false);

	//el shorts para reiniciar a 0
    nrf_rtc_shorts_enable(rtc2.p_reg, NRF_RTC_SHORT_COMPARE0_CLEAR_MASK);


   	//PPI
    nrf_ppi_channel_t ppi_channel;
    err = nrfx_ppi_channel_alloc(&ppi_channel);
    if (err != NRFX_SUCCESS) {
        LOG_ERR("Error: No PPI channel available.\n");
        return;
    }
    
    /* Obtenemos las direcciones de memoria del evento (RTC) y la tarea (GPIOTE) */
    uint32_t rtc_event_addr = nrfx_rtc_event_address_get(&rtc2, NRF_RTC_EVENT_COMPARE_0);
    uint32_t gpiote_task_addr = nrfx_gpiote_out_task_address_get(EXTCOMIN_PIN);

    //con el evento del rtc se activa la task del gpiote (que es toggle)
    err = nrfx_ppi_channel_assign(ppi_channel, rtc_event_addr, gpiote_task_addr);
    
    err = nrfx_ppi_channel_enable(ppi_channel);

	
    nrfx_rtc_enable(&rtc2);
    
    LOG_INF("Hardware VCOM inversion initialized.\n");
}