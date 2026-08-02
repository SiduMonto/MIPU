#include "gui.h"
#include <lvgl.h>
#include <stdio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(gui, LOG_LEVEL_INF);

#define WHITE_COLOR 0xFFFFFF
#define BLACK_COLOR 0x000000

#define MAX_MENU_ITEMS 10 // Límite seguro para inicializar arrays

/* Puntero al estado global de la app (guardado en el init) */
static const struct app_state *app_state;

/* Punteros a las pantallas */
static lv_obj_t *screen_home;
static lv_obj_t *screen_menu;
static lv_obj_t *screen_settings;
static lv_obj_t *screen_about;

/* Elementos de HOME*/
static lv_obj_t *status_batt_label;
static lv_obj_t *status_bt_label;
static lv_obj_t *home_time_label;
static lv_obj_t *home_date_label;

static const char * const * menu_items_text; // Puntero a la lista de strings del menu

static size_t num_menu_items;

static lv_obj_t *menu_items_containers[MAX_MENU_ITEMS];
static lv_obj_t *menu_items_labels[MAX_MENU_ITEMS];

/* Estado actual renderizado */
static enum app_screen current_active_screen = APP_SCREEN_HOME;

/* Estilos globales y reutilizables */
static lv_style_t style_screen;
static lv_style_t style_text_normal;
static lv_style_t style_normal;    // Fondo blanco, texto negro
static lv_style_t style_inverted;  // Fondo negro, texto blanco

static void init_styles(void){
	// Estilo de las pantallas (fondo blanco sólido para la Sharp MIP)
	lv_style_init(&style_screen);
	lv_style_set_bg_color(&style_screen, lv_color_hex(WHITE_COLOR));
	lv_style_set_bg_opa(&style_screen, LV_OPA_COVER);
	lv_style_set_text_color(&style_screen, lv_color_hex(BLACK_COLOR));

	lv_style_init(&style_text_normal);
	lv_style_set_text_color(&style_text_normal, lv_color_hex(BLACK_COLOR));

	// Estilo Normal
	lv_style_init(&style_normal);
	lv_style_set_bg_color(&style_normal, lv_color_hex(WHITE_COLOR));
	lv_style_set_text_color(&style_normal, lv_color_hex(BLACK_COLOR));
	lv_style_set_radius(&style_normal, 0); // Sin bordes redondeados (para MIP :))
	lv_style_set_border_width(&style_normal, 0);

	// Estilo Invertido
	lv_style_init(&style_inverted);
	lv_style_set_bg_color(&style_inverted, lv_color_hex(BLACK_COLOR)); 
	lv_style_set_text_color(&style_inverted, lv_color_hex(WHITE_COLOR)); 
	lv_style_set_radius(&style_inverted, 0);
	lv_style_set_border_width(&style_inverted, 0);
}

//PANTALLA HOME
static void create_screen_home(void){
	screen_home = lv_obj_create(NULL);
	lv_obj_add_style(screen_home, &style_screen, LV_PART_MAIN);

	// Bluetooth en la esquina superior izquierda
	status_bt_label = lv_label_create(screen_home);
	lv_obj_add_style(status_bt_label, &style_text_normal, LV_PART_MAIN);
	lv_label_set_text(status_bt_label, "--");
	lv_obj_align(status_bt_label, LV_ALIGN_TOP_LEFT, 5, 5);

	// Batería en la esquina superior derecha
	status_batt_label = lv_label_create(screen_home);
	lv_obj_add_style(status_batt_label, &style_text_normal, LV_PART_MAIN);
	lv_label_set_text(status_batt_label, "100%");
	lv_obj_align(status_batt_label, LV_ALIGN_TOP_RIGHT, -5, 5);

	// Reloj central gigante
	home_time_label = lv_label_create(screen_home);
	lv_obj_add_style(home_time_label, &style_text_normal, LV_PART_MAIN);
	lv_label_set_text(home_time_label, "00:00");
	lv_obj_align(home_time_label, LV_ALIGN_CENTER, 0, -10);

	// Fecha debajo
	home_date_label = lv_label_create(screen_home);
	lv_obj_add_style(home_date_label, &style_text_normal, LV_PART_MAIN);
	lv_label_set_text(home_date_label, "AVOCADO DAY");
	lv_obj_align_to(home_date_label, home_time_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
}

//PANTALLA MENU
static void create_screen_menu(void){
    screen_menu = lv_obj_create(NULL);
    lv_obj_add_style(screen_menu, &style_screen, LV_PART_MAIN);

    // Título del menú (fijo en la parte superior)
    lv_obj_t *title = lv_label_create(screen_menu);
    lv_label_set_text(title, "MENU");
    lv_obj_add_style(title, &style_text_normal, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

    // Contenedor dedicado para la lista (Maneja el flexbox y el scroll)
    lv_obj_t *list_cont = lv_obj_create(screen_menu);
    lv_obj_set_size(list_cont, lv_pct(100), lv_pct(80)); // Ocupa el 80% inferior
    lv_obj_align(list_cont, LV_ALIGN_BOTTOM_MID, 0, 0);
    
    // Hacemos el contenedor de la lista invisible para que no interfiera con tus estilos
    lv_obj_set_style_bg_opa(list_cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(list_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list_cont, 4, LV_PART_MAIN); // Espaciado vertical entre botones
    lv_obj_set_style_radius(list_cont, 0, LV_PART_MAIN);

    // Habilitar layout Flex de columna
    lv_obj_set_layout(list_cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Lista de opciones generada dinámicamente
    for (size_t i = 0; i < num_menu_items; i++) {
        menu_items_containers[i] = lv_obj_create(list_cont);
        
        // Ancho porcentual (92% de la pantalla) y alto dinámico basado en el contenido
        lv_obj_set_size(menu_items_containers[i], lv_pct(92), LV_SIZE_CONTENT); 
        lv_obj_set_style_pad_all(menu_items_containers[i], 4, LV_PART_MAIN); 

        menu_items_labels[i] = lv_label_create(menu_items_containers[i]);
        lv_label_set_text(menu_items_labels[i], menu_items_text[i]);
        lv_obj_align(menu_items_labels[i], LV_ALIGN_CENTER, 0, 0);
        
        // Aplicar estilo normal por defecto
        lv_obj_add_style(menu_items_containers[i], &style_normal, LV_PART_MAIN);
    }
}

//PANTALLA SIMPLE
static void create_screen_simple(lv_obj_t **screen_ptr, const char *title_text){
	*screen_ptr = lv_obj_create(NULL);
	lv_obj_add_style(*screen_ptr, &style_screen, LV_PART_MAIN);

	lv_obj_t *title = lv_label_create(*screen_ptr);
	lv_obj_add_style(title, &style_text_normal, LV_PART_MAIN);
	lv_label_set_text(title, title_text);
	lv_obj_align(title, LV_ALIGN_CENTER, 0, -15);

	lv_obj_t *desc = lv_label_create(*screen_ptr);
	lv_obj_add_style(desc, &style_text_normal, LV_PART_MAIN);
	lv_label_set_text(desc, "[Long Press]\nto exit");
	lv_obj_align(desc, LV_ALIGN_CENTER, 0, 15);
}

/*Inicialización y Lógica de Actualización */
void gui_init(const struct app_state *state, const char *const menu_items[], const size_t menu_items_length){
	LOG_INF("Iniciando GUI para Pantalla MIP monocroma...");

	menu_items_text = menu_items;
	num_menu_items = menu_items_length;

	init_styles();

	create_screen_home();
	create_screen_menu();
	create_screen_simple(&screen_settings, "SETTINGS\n(WIP)");
	create_screen_simple(&screen_about, "ABOUT\nMIP Watch v1");

	// Cargar Home sin animaciones
	lv_scr_load_anim(screen_home, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);

	app_state = state; //lo pongo al final para que gui_update falle hasta que esto haya terminado

	}

	void gui_update(void)
	{
	if (!app_state) return; //porsiaca

	// cambio de pantallas
	if (app_state->screen != current_active_screen) {
		current_active_screen = app_state->screen;
		switch (app_state->screen) {
			case APP_SCREEN_HOME:     lv_scr_load_anim(screen_home, LV_SCR_LOAD_ANIM_NONE, 0, 0, false); break;
			case APP_SCREEN_MENU:     lv_scr_load_anim(screen_menu, LV_SCR_LOAD_ANIM_NONE, 0, 0, false); break;
			case APP_SCREEN_SETTINGS: lv_scr_load_anim(screen_settings, LV_SCR_LOAD_ANIM_NONE, 0, 0, false); break;
			case APP_SCREEN_ABOUT:    lv_scr_load_anim(screen_about, LV_SCR_LOAD_ANIM_NONE, 0, 0, false); break;
		}
	}

	//=========================================
	// Actualizaciones específicas por pantalla
	//=========================================

	//home
	if (current_active_screen == APP_SCREEN_HOME) {
		// Actualizamos estado de Batería y BT solo cuando estamos en Home
		lv_label_set_text_fmt(status_batt_label, "%d%%", app_state->battery_percent);
		lv_label_set_text(status_bt_label, app_state->bt_ready ? "BT" : "--");

		// Actualizamos Hora y Fecha
		struct tm now_tm;
		if (app_get_current_tm(app_state, &now_tm)) {
			lv_label_set_text_fmt(home_time_label, "%02d:%02d", now_tm.tm_hour, now_tm.tm_min);
			lv_label_set_text_fmt(home_date_label, "%02d/%02d/%04d", 
								now_tm.tm_mday, now_tm.tm_mon + 1, now_tm.tm_year + 1900);
		} else {
			lv_label_set_text(home_time_label, "--:--");
			lv_label_set_text(home_date_label, "AVOCADO DAY");
		}
	} 
	//menu
	else if (current_active_screen == APP_SCREEN_MENU) {
		// Actualizamos los estilos para mostrar cuál está seleccionado
		for (size_t i = 0; i < num_menu_items; i++) {
			if (i == app_state->menu_index) {
				lv_obj_add_style(menu_items_containers[i], &style_inverted, LV_PART_MAIN);
				lv_obj_remove_style(menu_items_containers[i], &style_normal, LV_PART_MAIN);

				lv_obj_scroll_to_view(menu_items_containers[i], LV_ANIM_OFF);
			} else {
				lv_obj_add_style(menu_items_containers[i], &style_normal, LV_PART_MAIN);
				lv_obj_remove_style(menu_items_containers[i], &style_inverted, LV_PART_MAIN);
			}
		}
	}
}