#ifndef BATTERY_H
#define BATTERY_H

#include "app.h"

/**
 * @brief Inicializa el ADC, los pines de la batería y lanza la primera lectura.
 * 
 * @param state Puntero al estado global de la app para actualizar el porcentaje.
 */
void battery_init(struct app_state *state);

#endif /* BATTERY_H */