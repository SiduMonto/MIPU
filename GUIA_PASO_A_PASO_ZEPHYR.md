# MIPU: guia paso a paso para el primer smartwatch con Zephyr

Este documento es una version practica y mas directa de la guia general. La idea es que puedas copiar cada bloque, entender que hace, y tener una base de firmware que ya sirva para el primer prototipo.

## Enfoque de esta primera version

Voy a asumir estas decisiones:

- Placa: XIAO nRF52840.
- Pantalla final: Sharp Memory LCD monocromatica.
- Alimentacion: LiPo.
- Botones fisicos todavia no disponibles.
- Simulacion de botones con teclado/USB serial.
- Teclas cortas: `1`, `2`, `3`.
- Teclas largas: `Q`, `W`, `E`.
- Hora: usar el reloj del sistema de Zephyr, que en nRF52840 va sobre RTC interno, y sincronizarlo de vez en cuando con Bluetooth CTS.
- No meter un estado extra de "time synced". Solo guardar si el reloj valido existe o no.

La meta no es hacer el producto final todavia. La meta es tener una base limpia, entendible y ampliable.

## Paso 0: que archivos tocar

Para esta primera version, toca solo estos archivos:

- [CMakeLists.txt](CMakeLists.txt)
- [prj.conf](prj.conf)
- [src/app.h](src/app.h)
- [src/app.c](src/app.c)
- [src/main.c](src/main.c)

El overlay [xiao_ble.overlay](xiao_ble.overlay) puede quedarse como esta ahora mismo, porque ya te deja consola por USB y ya prepara el display SPI.

## Paso 1: actualizar `CMakeLists.txt`

Si ahora mismo tu `CMakeLists.txt` solo compila `src/main.c`, cambialo para compilar tambien la logica de app.

```cmake
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(MIPU)

target_sources(app PRIVATE
    src/main.c
    src/app.c
)
```
## Paso 2: dejar `prj.conf` mas claro

Tu `prj.conf` actual ya va bien encaminado. Para esta version, dejalo asi:

```conf
# Energia
CONFIG_PM=y
CONFIG_PM_DEVICE=y
CONFIG_SYS_CLOCK_EXISTS=y

# Perifericos
CONFIG_GPIO=y
CONFIG_SPI=y
CONFIG_ADC=y

# Bluetooth y hora
CONFIG_BT=y
CONFIG_BT_PERIPHERAL=y
CONFIG_BT_NRF_SERVICES=y
CONFIG_BT_CTS_CLIENT=y

# Fecha y hora
CONFIG_DATE_TIME=y
CONFIG_DATE_TIME_NTP=n

# Pantalla y LVGL
CONFIG_DISPLAY=y
CONFIG_LVGL=y
CONFIG_LV_COLOR_DEPTH_1=y
CONFIG_LV_Z_VDB_SIZE=100
CONFIG_LV_Z_FULL_REFRESH=y
CONFIG_LV_Z_MEM_POOL_SIZE=8192

# Logging
CONFIG_LOG=y
CONFIG_LOG_MODE_IMMEDIATE=y

# Consola serie por USB
CONFIG_SERIAL=y
CONFIG_CONSOLE=y
CONFIG_UART_CONSOLE=y
CONFIG_UART_LINE_CTRL=y

# USB CDC ACM
CONFIG_USB_DEVICE_STACK_NEXT=y
CONFIG_USBD_CDC_ACM_CLASS=y

# Stack un poco mas comodo para arrancar
CONFIG_MAIN_STACK_SIZE=2048
```

Si luego quieres usar una RTC dedicada para un reloj de app separado, ese sera otro paso. En esta primera version no hace falta.

## Paso 3: crear `src/app.h`

Este archivo define el estado de la app y las funciones basicas.

```c
#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

struct bt_cts_current_time;

enum app_screen {
    APP_SCREEN_HOME = 0,
    APP_SCREEN_MENU,
    APP_SCREEN_SETTINGS,
    APP_SCREEN_ABOUT,
};

enum app_button_event {
    APP_BTN_PREV_SHORT = 0,
    APP_BTN_OK_SHORT,
    APP_BTN_NEXT_SHORT,
    APP_BTN_PREV_LONG,
    APP_BTN_OK_LONG,
    APP_BTN_NEXT_LONG,
};

enum app_action {
    APP_ACTION_NONE = 0,
    APP_ACTION_SYNC_NOW,
};

struct app_clock_state {
    bool valid;
    int64_t base_epoch_ms;
    int64_t base_uptime_ms;
};

struct app_state {
    enum app_screen screen;
    int menu_index;
    bool bt_ready;
    bool sync_in_progress;
    bool last_sync_ok;
    int battery_percent;
    struct app_clock_state clock;
};

void app_init(struct app_state *state);
enum app_action app_handle_button(struct app_state *state, enum app_button_event event);
void app_set_bt_ready(struct app_state *state, bool ready);
void app_set_battery_percent(struct app_state *state, int percent);
void app_request_sync(struct app_state *state);
void app_set_time_from_cts(struct app_state *state,
                           const struct bt_cts_current_time *current_time,
                           int err);
bool app_get_current_tm(const struct app_state *state, struct tm *out);
void app_render(const struct app_state *state);

#endif
```

## Paso 4: crear `src/app.c`

Aqui va la logica real de la app: pantallas, botones y reloj.

```c
#include "app.h"

#include <date_time.h>
#include <bluetooth/services/cts_client.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/timeutil.h>

static const char *screen_name(enum app_screen screen)
{
    switch (screen) {
    case APP_SCREEN_HOME:
        return "HOME";
    case APP_SCREEN_MENU:
        return "MENU";
    case APP_SCREEN_SETTINGS:
        return "SETTINGS";
    case APP_SCREEN_ABOUT:
        return "ABOUT";
    default:
        return "UNKNOWN";
    }
}

static const char *const menu_items[] = {
    "Home",
    "Sync now",
    "Battery",
    "About",
};

void app_init(struct app_state *state)
{
    memset(state, 0, sizeof(*state));
    state->screen = APP_SCREEN_HOME;
    state->menu_index = 0;
    state->battery_percent = 100;
}

void app_set_bt_ready(struct app_state *state, bool ready)
{
    state->bt_ready = ready;
}

void app_set_battery_percent(struct app_state *state, int percent)
{
    if (percent < 0) {
        percent = 0;
    }

    if (percent > 100) {
        percent = 100;
    }

    state->battery_percent = percent;
}

void app_request_sync(struct app_state *state)
{
    state->sync_in_progress = true;
}

void app_set_time_from_cts(struct app_state *state,
                           const struct bt_cts_current_time *current_time,
                           int err)
{
    if (err != 0 || current_time == NULL) {
        state->sync_in_progress = false;
        state->last_sync_ok = false;
        return;
    }

    struct tm tm = {
        .tm_year = (int)current_time->exact_time_256.year - 1900,
        .tm_mon = (int)current_time->exact_time_256.month - 1,
        .tm_mday = (int)current_time->exact_time_256.day,
        .tm_hour = (int)current_time->exact_time_256.hours,
        .tm_min = (int)current_time->exact_time_256.minutes,
        .tm_sec = (int)current_time->exact_time_256.seconds,
        .tm_isdst = -1,
    };

    int64_t unix_s = timeutil_timegm64(&tm);
    if (unix_s < 0) {
        state->sync_in_progress = false;
        state->last_sync_ok = false;
        return;
    }

    int64_t frac_ms = ((int64_t)current_time->exact_time_256.fractions256 * 1000) / 256;

    state->clock.base_epoch_ms = (unix_s * 1000) + frac_ms;
    state->clock.base_uptime_ms = k_uptime_get();
    state->clock.valid = true;
    state->sync_in_progress = false;
    state->last_sync_ok = true;

    (void)date_time_set(&tm);
}

bool app_get_current_tm(const struct app_state *state, struct tm *out)
{
    if (!state->clock.valid || out == NULL) {
        return false;
    }

    int64_t now_ms = state->clock.base_epoch_ms + (k_uptime_get() - state->clock.base_uptime_ms);
    time_t now_s = (time_t)(now_ms / 1000);

    return gmtime_r(&now_s, out) != NULL;
}

enum app_action app_handle_button(struct app_state *state, enum app_button_event event)
{
    enum app_action action = APP_ACTION_NONE;

    switch (state->screen) {
    case APP_SCREEN_HOME:
        if (event == APP_BTN_PREV_SHORT) {
            state->screen = APP_SCREEN_MENU;
        } else if (event == APP_BTN_OK_SHORT) {
            state->screen = APP_SCREEN_MENU;
        } else if (event == APP_BTN_NEXT_SHORT) {
            state->screen = APP_SCREEN_SETTINGS;
        } else if (event == APP_BTN_OK_LONG) {
            action = APP_ACTION_SYNC_NOW;
        }
        break;

    case APP_SCREEN_MENU:
        if (event == APP_BTN_PREV_SHORT) {
            state->menu_index = (state->menu_index + 3) % 4;
        } else if (event == APP_BTN_NEXT_SHORT) {
            state->menu_index = (state->menu_index + 1) % 4;
        } else if (event == APP_BTN_OK_SHORT) {
            switch (state->menu_index) {
            case 0:
                state->screen = APP_SCREEN_HOME;
                break;
            case 1:
                action = APP_ACTION_SYNC_NOW;
                break;
            case 2:
                state->screen = APP_SCREEN_SETTINGS;
                break;
            case 3:
                state->screen = APP_SCREEN_ABOUT;
                break;
            }
        } else if (event == APP_BTN_OK_LONG) {
            action = APP_ACTION_SYNC_NOW;
        }
        break;

    case APP_SCREEN_SETTINGS:
        if (event == APP_BTN_OK_SHORT || event == APP_BTN_PREV_SHORT || event == APP_BTN_NEXT_SHORT) {
            state->screen = APP_SCREEN_HOME;
        } else if (event == APP_BTN_OK_LONG) {
            action = APP_ACTION_SYNC_NOW;
        }
        break;

    case APP_SCREEN_ABOUT:
        if (event == APP_BTN_OK_SHORT || event == APP_BTN_PREV_SHORT || event == APP_BTN_NEXT_SHORT) {
            state->screen = APP_SCREEN_HOME;
        } else if (event == APP_BTN_OK_LONG) {
            action = APP_ACTION_SYNC_NOW;
        }
        break;
    }

    return action;
}

void app_render(const struct app_state *state)
{
    struct tm now_tm;
    char time_buf[32];
    const char *sync_text = state->sync_in_progress ? "SYNCING" : (state->last_sync_ok ? "OK" : "NO SYNC");

    printk("\033[2J\033[H");
    printk("==============================\n");
    printk(" MIPU watch prototype\n");
    printk("==============================\n\n");
    printk("Screen : %s\n", screen_name(state->screen));
    printk("BT     : %s\n", state->bt_ready ? "READY" : "OFF");
    printk("Clock  : %s\n", sync_text);
    printk("Battery: %d%%\n\n", state->battery_percent);

    if (app_get_current_tm(state, &now_tm)) {
        snprintf(time_buf, sizeof(time_buf), "%04d-%02d-%02d %02d:%02d:%02d",
                 now_tm.tm_year + 1900,
                 now_tm.tm_mon + 1,
                 now_tm.tm_mday,
                 now_tm.tm_hour,
                 now_tm.tm_min,
                 now_tm.tm_sec);
        printk("Time   : %s UTC\n\n", time_buf);
    } else {
        printk("Time   : --:--:--\n\n");
    }

    if (state->screen == APP_SCREEN_MENU) {
        printk("Menu:\n");
        for (int i = 0; i < 4; ++i) {
            printk("%c %s\n", (i == state->menu_index) ? '>' : ' ', menu_items[i]);
        }
        printk("\n");
    } else if (state->screen == APP_SCREEN_SETTINGS) {
        printk("Settings:\n");
        printk("- Sync hora\n");
        printk("- Battery placeholder\n");
        printk("- Future display settings\n\n");
    } else if (state->screen == APP_SCREEN_ABOUT) {
        printk("About:\n");
        printk("- Zephyr smartwatch prototype\n");
        printk("- Sharp Memory LCD planned\n");
        printk("- Buttons simulated with keyboard\n\n");
    }

    printk("Keys: 1 prev | 2 ok | 3 next | Q/W/E long press\n");
}
```

## Paso 5: reemplazar `src/main.c`

Este `main.c` inicializa USB, Bluetooth y la lectura por teclado. Tambien dispara la sincronizacion CTS cuando hace falta.

```c
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <bluetooth/services/cts_client.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/drivers/uart.h>

#include "app.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

USBD_DEVICE_DEFINE(my_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 0x1234, 0x5678);
USBD_DESC_LANG_DEFINE(lang_desc);
USBD_DESC_MANUFACTURER_DEFINE(mfg_desc, "Seeed");
USBD_DESC_PRODUCT_DEFINE(product_desc, "XIAO BLE Console");

static struct bt_cts_client cts_client;
static struct app_state app;
static const struct device *const console_uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

static void cts_time_cb(struct bt_cts_client *cts,
                        struct bt_cts_current_time *current_time,
                        int err);

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

    app_render(&app);
}

static int usb_console_start(void)
{
    int err;

    err = usbd_add_descriptor(&my_usbd, &lang_desc);
    err |= usbd_add_descriptor(&my_usbd, &mfg_desc);
    err |= usbd_add_descriptor(&my_usbd, &product_desc);
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
    int err = bt_enable(NULL);
    if (err) {
        return err;
    }

    bt_cts_client_init(&cts_client);
    app_set_bt_ready(&app, true);
    return 0;
}

int main(void)
{
    int err;
    int64_t last_render_ms = 0;

    app_init(&app);

    if (!device_is_ready(console_uart)) {
        LOG_ERR("Console UART not ready");
        return 0;
    }

    err = usb_console_start();
    if (err) {
        LOG_ERR("USB init failed: %d", err);
        return err;
    }

    LOG_INF("Consola USB iniciada correctamente");

    err = bluetooth_start();
    if (err) {
        LOG_WRN("Bluetooth init failed: %d", err);
    }

    app_render(&app);
    request_time_sync();

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
            app_render(&app);

            if (action == APP_ACTION_SYNC_NOW) {
                request_time_sync();
            }
        }

        int64_t now_ms = k_uptime_get();
        if ((now_ms - last_render_ms) >= 1000) {
            app_render(&app);
            last_render_ms = now_ms;
        }

        k_sleep(K_MSEC(20));
    }
}
```

## Paso 6: como funciona este reloj

La pieza importante aqui es esta:

- CTS te da la hora real cuando el telefono responde.
- `timeutil_timegm64()` convierte esa hora a segundos Unix.
- `k_uptime_get()` mide el paso del tiempo desde el arranque.
- Tu app guarda una referencia:
  - hora Unix sincronizada
  - instante de uptime en el que se sincronizo
- Luego, para dibujar la hora, suma el uptime transcurrido a la hora base.

Eso hace que el reloj siga andando aunque no vuelvas a pedir CTS constantemente.

## Paso 7: por que esta version es buena para tu primer prototipo

Porque te da ya estas cosas:

- Un arranque limpio.
- USB consola funcionando.
- Bluetooth CTS listo.
- Un reloj que avanza solo entre sincronizaciones.
- Un sistema de pantallas simple.
- Simulacion de botones sin depender del hardware fisico.

## Paso 8: como se traduce esto luego a la Sharp Memory LCD

Cuando llegue la pantalla, no tires esta logica. Solo cambia la capa de renderizado.

La estructura que ya tienes te deja hacer esto:

- `app.c` decide que pantalla toca.
- `main.c` lee teclas y sincroniza hora.
- Un futuro `ui_lvgl.c` o `display.c` pinta la interfaz real.

En otras palabras: la pantalla deja de imprimir por consola y empieza a pintar widgets, pero el estado de la app se queda igual.

## Paso 9: siguiente mejora recomendada

Cuando este primer esqueleto ya compile y arranque, el siguiente paso bueno es este:

1. Separar el render de consola en un archivo `src/ui_console.c`.
2. Crear `src/ui_lvgl.c` con el mismo estado, pero ya dibujando en la Sharp Memory LCD.
3. Anadir lectura real de bateria por ADC.
4. Anadir un temporizador de sincronizacion diaria, pero sin bloquear el arranque.

## Paso 10: si quieres usar una RTC dedicada mas adelante

Ahora mismo no la necesitas para avanzar.

Mas adelante, si quieres que el reloj siga incluso con modos de ahorro mas agresivos, puedes mover el tiempo base a una RTC dedicada de la app. La idea seria:

- dejar el sistema usando su RTC interna para Zephyr/Bluetooth,
- reservar otra instancia para el reloj del reloj de pulsera,
- y mantener esta misma arquitectura de pantallas.

## Resumen corto

Si solo quieres saber que hacer primero:

1. Actualiza `prj.conf`.
2. Copia `src/app.h` y `src/app.c`.
3. Sustituye `src/main.c` por el de arriba.
4. Arranca con consola USB.
5. Usa `1/2/3` y `Q/W/E` para moverte.
6. Cuando todo eso funcione, pasas el render a la Sharp Memory LCD.
