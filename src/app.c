#include "app.h"

//#include <date_time.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/timeutil.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#define CONFIG_ENABLE_TERMINAL_UI 0

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

const char *const menu_items[] = {
    "Home",
    "Sync Time",
    "Settings",
    "About",
};

const size_t menu_length = sizeof(menu_items) / sizeof(menu_items[0]);

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
    state->battery_percent = percent;
}

void app_request_sync(struct app_state *state)
{
    state->sync_in_progress = true;
}


#ifdef CONFIG_BT
#include <bluetooth/services/cts_client.h>
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

    //bloqueo porque son 64 bits, podria ser interrumpido a mitad de variable
    k_spinlock_key_t key = k_spin_lock(&state->lock);
    state->clock.base_epoch_ms = (unix_s * 1000) + frac_ms;
    state->clock.base_uptime_ms = k_uptime_get();
    state->clock.valid = true;
    k_spin_unlock(&state->lock, key);

    state->sync_in_progress = false;
    state->last_sync_ok = true;

    //(void)date_time_set(&tm);
}
#endif

bool app_get_current_tm(const struct app_state *state, struct tm *out)
{
    if (!state->clock.valid || out == NULL) {
        return false;
    }

    //bloqueo porque son 64 bits, podria ser interrumpido a mitad de variable
    k_spinlock_key_t key = k_spin_lock((struct k_spinlock *)&state->lock);
    int64_t now_ms = state->clock.base_epoch_ms + (k_uptime_get() - state->clock.base_uptime_ms);
    k_spin_unlock((struct k_spinlock *)&state->lock, key);

    time_t now_s = (time_t)(now_ms / 1000);

    return gmtime_r(&now_s, out) != NULL;
}

enum app_action app_handle_button(struct app_state *state, enum app_button_event event)
{
    enum app_action action = APP_ACTION_NONE;
    enum app_screen old_screen = state->screen;

    switch (state->screen) {
    case APP_SCREEN_HOME:
        if (event == APP_BTN_PREV_SHORT) {
            state->screen = APP_SCREEN_MENU;
        } else if (event == APP_BTN_OK_SHORT) {
            state->screen = APP_SCREEN_MENU; //temporal ya lo cambiare
        } else if (event == APP_BTN_NEXT_SHORT) {
            state->screen = APP_SCREEN_SETTINGS;
        } else if (event == APP_BTN_OK_LONG) { //si pulsacion larga, sincronizo
            action = APP_ACTION_SYNC_NOW;
        }
        break;

    case APP_SCREEN_MENU:
        if (event == APP_BTN_PREV_SHORT) {
            state->menu_index = (state->menu_index + menu_length - 1) % menu_length;
        } else if (event == APP_BTN_NEXT_SHORT) {
            state->menu_index = (state->menu_index + 1) % menu_length;
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
            state->screen = APP_SCREEN_HOME;
        }
        break;

    case APP_SCREEN_SETTINGS:
        if (event == APP_BTN_OK_SHORT || event == APP_BTN_PREV_SHORT || event == APP_BTN_NEXT_SHORT) {
           // state->screen = APP_SCREEN_HOME;
           //no hace nada por ahora
        } else if (event == APP_BTN_OK_LONG) {
            state->screen = APP_SCREEN_HOME;
        }
        break;

    case APP_SCREEN_ABOUT:
        if (event == APP_BTN_OK_SHORT || event == APP_BTN_PREV_SHORT || event == APP_BTN_NEXT_SHORT) {
            //state->screen = APP_SCREEN_HOME;
            //no hace nada por ahora
        } else if (event == APP_BTN_OK_LONG) {
            state->screen = APP_SCREEN_HOME;
        }
        break;
    }

    LOG_INF("Nueva pantalla: %d", state->screen);
    if (action != APP_ACTION_NONE) {
        LOG_INF("Accion tomada: %d", action);
    } else {
        LOG_INF("No ha habido accion");
    }
    if(old_screen != state->screen && action == APP_ACTION_NONE){
        action = APP_ACTION_SCREEN_CHANGE;
    }
    return action;
}

void app_render(const struct app_state *state)
{
    #if CONFIG_ENABLE_TERMINAL_UI
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
            for (int i = 0; i < menu_length; ++i) {
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
    #else
        LOG_INF("Screen: %s | BT: %d | Batt: %d%%", 
            screen_name(state->screen), 
            state->bt_ready, 
            state->battery_percent);
    #endif
}