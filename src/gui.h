#ifndef GUI_H
#define GUI_H

#include "app.h"

/**
 * @brief Inicializa la interfaz gráfica de LVGL.
 * @param state Puntero global al estado de la aplicación.
 * La GUI guardará esta referencia para futuras actualizaciones.
 */
void gui_init(const struct app_state *state, const char *const menu_items[]);

/**
 * @brief Actualiza los elementos visuales basándose en el estado interno guardado.
 * Debe llamarse periódicamente en el bucle principal.
 */
void gui_update(void);

#endif /* GUI_H */