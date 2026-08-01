#ifndef APP_H
#define APP_H

#include <zephyr/spinlock.h>
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
    struct k_spinlock lock;
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