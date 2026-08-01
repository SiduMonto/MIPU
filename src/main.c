#include <zephyr/kernel.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <bluetooth/gatt_dm.h>
#include <bluetooth/services/cts_client.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <zephyr/usb/usbd.h>
#include <zephyr/drivers/uart.h>

#include "app.h"
#include "battery.h"
#include "inversion.h"

#define DEVICE_NAME "MIPU Watch"
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

//pa la uart creo
USBD_DEVICE_DEFINE(my_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 0x1234, 0x5678);
USBD_CONFIGURATION_DEFINE(my_config, USB_SCD_SELF_POWERED, 100, NULL);
USBD_DESC_LANG_DEFINE(lang_desc);
USBD_DESC_MANUFACTURER_DEFINE(mfg_desc, "Seeed");
USBD_DESC_PRODUCT_DEFINE(product_desc, "XIAO BLE Console");

static struct bt_conn *default_conn = NULL;

static struct bt_cts_client cts_client;
static struct app_state app;
static const struct device *const console_uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

//BLUETOOTH TIME SYNC
//TODO: poner que todos los dias se sincronice. si falla no pasa nada porque hay que hacer que al conectarse pida la hora       
// Esta función se ejecuta automáticamente cuando el móvil responde con la hora
static void cts_time_cb(struct bt_cts_client *cts, struct bt_cts_current_time *current_time, int err);
static void request_time_sync(void);

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};


// // Variables globales para el proceso de búsqueda
// static struct bt_gatt_discover_params discover_params;
// static struct bt_uuid_16 uuid = BT_UUID_INIT_16(0);

// // 2. Callback: Zephyr nos llama cuando el móvil responde
// static uint8_t discover_func(struct bt_conn *conn,
//                              const struct bt_gatt_attr *attr,
//                              struct bt_gatt_discover_params *params)
// {
//     if (!attr) {
//         LOG_INF("GATT Discovery completado.");
        
//         // ¡El reloj ya no está ciego! Ahora sí pedimos la hora.
//         request_time_sync();
        
//         // Limpiamos los parámetros para futuros usos
//         memset(params, 0, sizeof(*params));
//         return BT_GATT_ITER_STOP;
//     }

//     LOG_INF("Encontrado servicio en el handle %u", attr->handle);
//     return BT_GATT_ITER_CONTINUE;
// }

// // 1. Función para iniciar la búsqueda
// static void start_gatt_discovery(struct bt_conn *conn)
// {
//     int err;

//     // Configuramos la búsqueda para el servicio CTS (UUID 0x1805)
//     memcpy(&uuid, BT_UUID_CTS, sizeof(uuid));
    
//     discover_params.uuid = &uuid.uuid;
//     discover_params.func = discover_func;
//     discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
//     discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
//     discover_params.type = BT_GATT_DISCOVER_PRIMARY; // Buscamos servicios principales

//     err = bt_gatt_discover(conn, &discover_params);
//     if (err) {
//         LOG_ERR("Fallo al iniciar Discovery (err %d)", err);
//     } else {
//         LOG_INF("Iniciando GATT Discovery para CTS...");
//     }
// }


static void discovery_completed(struct bt_gatt_dm *dm, void *context)
{
    LOG_INF("CTS service discovered");

    // Vinculamos lo que ha encontrado el DM con nuestro cliente CTS
    bt_cts_handles_assign(dm, &cts_client);
    

    request_time_sync();


    //libero la memoria del Discovery Manager
    bt_gatt_dm_data_release(dm);
}

// Se ejecuta si el movil no tiene el servicio de hora
static void discovery_service_not_found(struct bt_conn *conn, void *context)
{
    LOG_WRN("Device connected does not expose CTS service.");
}

// Se ejecuta si hubo un error de conexión durante la búsqueda
static void discovery_error(struct bt_conn *conn, int err, void *context)
{
    LOG_ERR("Error during GATT Discovery (err %d)", err);
}

// Estructura que agrupa los callbacks del GATT DM
static const struct bt_gatt_dm_cb discovery_cb = {
    .completed = discovery_completed,
    .service_not_found = discovery_service_not_found,
    .error_found = discovery_error,
};

/* ========================================================
 * CALLBACKS DE SEGURIDAD (PAIRING)
 * ======================================================== */
static void pairing_complete(struct bt_conn *conn, bool bonded)
{
    LOG_INF("Pairing complete. Saved in Flash: %s", 
            bonded ? "Yes" : "No");
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
    LOG_WRN("Pairing failed or canceled. (Reason: %d)", reason);
}

// Estructura que registra nuestras funciones de información de seguridad
static struct bt_conn_auth_info_cb auth_info_callbacks = {
    .pairing_complete = pairing_complete,
    .pairing_failed = pairing_failed,
};


//CALLBACK de seguridad cambiada
static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
    if (!err && level >= BT_SECURITY_L2) {
        LOG_INF("Security set (level %d). Discovering CTS...", level);
        
        //Arranco el discovery manager de nordic para buscar servicios
        int discover_err = bt_gatt_dm_start(conn, BT_UUID_CTS, &discovery_cb, NULL);
        if (discover_err) {
            LOG_ERR("Error initializing GATT DM: %d", discover_err);
        }
    } else {
        LOG_WRN("Failed to increase security: %d", err);
    }
}


static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_ERR("BT LE connection error: (err %u)", err);
        return;
    }

    LOG_INF("PHONE CONNECTED!");
    
    // Guardamos la conexión para usarla luego y sumamos 1 a su contador de uso
    default_conn = bt_conn_ref(conn);
    app_set_bt_ready(&app, true);
    
    //Se solicita nivel 2 de seguridad (just works, sin autenticacion)
    //si ya nos conoce el movil (en flash), lo hara automaticamente
    int sec_err = bt_conn_set_security(conn, BT_SECURITY_L2);
    if (sec_err) {
        LOG_WRN("Security petition failed (err %d)", sec_err);
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("Phone disconnected (reason: %u)", reason);

    if (default_conn) {
        // Liberamos la memoria de la conexión
        bt_conn_unref(default_conn);
        default_conn = NULL;
    }

    app_set_bt_ready(&app, false);

    //volvemos a anunciarmos
    int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        LOG_ERR("Error restarting advertising (err %d)", err);
    } else {
        LOG_INF("Advertising restarted. Waiting for reconnection...");
    }
}

// Esta macro de Zephyr registra los callbacks automáticamente en el sistema
BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
    .security_changed = security_changed,
};

// SIMULACION DE BOTONES CON TECLADO
static enum app_button_event key_to_event(char key, bool *valid)
{
    *valid = true;

    switch (key) {
    case '1':
        return APP_BTN_PREV_SHORT;
    case '2':
        return APP_BTN_OK_SHORT;
    case '3':
        return APP_BTN_NEXT_SHORT;
    case 'Q':
    case 'q':
        return APP_BTN_PREV_LONG;
    case 'W':
    case 'w':
        return APP_BTN_OK_LONG;
    case 'E':
    case 'e':
        return APP_BTN_NEXT_LONG;
    default:
        *valid = false;
        return APP_BTN_OK_SHORT;
    }
}

static void request_time_sync(void)
{
    int err;

    app_request_sync(&app);
    err = bt_cts_read_current_time(&cts_client, cts_time_cb);
    if (err < 0) {
        LOG_WRN("CTS read request failed: %d", err);
    }
}

static void cts_time_cb(struct bt_cts_client *cts,
                        struct bt_cts_current_time *current_time,
                        int err)
{
    (void)cts;

    app_set_time_from_cts(&app, current_time, err);
    if (err == 0) {
        LOG_INF("Hora sincronizada con CTS");
    } else {
        LOG_WRN("CTS callback error: %d", err);
    }
}

static int usb_console_start(void)
{
    int err;

    err = usbd_add_descriptor(&my_usbd, &lang_desc);
    err |= usbd_add_descriptor(&my_usbd, &mfg_desc);
    err |= usbd_add_descriptor(&my_usbd, &product_desc);

    err |= usbd_add_configuration(&my_usbd, USBD_SPEED_FS, &my_config);
    err |= usbd_register_class(&my_usbd, "cdc_acm_0", USBD_SPEED_FS, 1);    

    if (err) {
        return err;
    }
    

    err = usbd_init(&my_usbd);
    if (err) {
        return err;
    }

    err = usbd_enable(&my_usbd);
    if (err) {
        return err;
    }

    return 0;
}


static int bluetooth_start(void)
{
    //activo escucha eventos seguridad (pairing)
    bt_conn_auth_info_cb_register(&auth_info_callbacks);

    int err = bt_enable(NULL);
    if (err) {
        return err;
    }

    //Cargar emparejamientos de la flash (TODO: revisar que esto aqui bien)
    if (IS_ENABLED(CONFIG_SETTINGS)) {
        settings_load();
        LOG_INF("Settings loaded from flash.");
    }

    LOG_INF("Bluetooth initialized.");

    bt_cts_client_init(&cts_client);

    //advertising
    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        LOG_ERR("Error starting advertising (err %d)", err);
        return err;
    }

    LOG_INF("Advertising started as '%s'.", DEVICE_NAME);
    return 0;
}

int main(void)
{
    int err;
    
    app_init(&app);

    //inicializar y habilitar el stack USB
    if (!device_is_ready(console_uart)) {
        LOG_ERR("Console UART not ready");
        return 0;
    }

    err = usb_console_start();
    if (err) {
        LOG_ERR("USB init failed: %d", err);
        return err;
    }

    LOG_INF("USB init succesful.\n");
    
    //Aqui inicializo la pantalla
    init_hardware_vcom();

    err = bluetooth_start();
    if (err) {
        LOG_WRN("Bluetooth init failed: %d", err);
    }

    battery_init(&app);
    

    //a futuro esto seria un sleep forever y gestionaria por interrupcion.
    while (1) {
        uint8_t raw;
        char key;
        bool valid;

        while (uart_poll_in(console_uart, &raw) == 0) {
            key = (char)raw;

            if (key == '\r' || key == '\n') {
                continue;
            }

            enum app_button_event event = key_to_event(key, &valid);
            if (!valid) {
                continue;
            }

            enum app_action action = app_handle_button(&app, event);

            if (action == APP_ACTION_SYNC_NOW) {
                request_time_sync();
            }
            //MAS ACCIONES...
        }
        k_sleep(K_MSEC(20));
    }
    return 0;
}
