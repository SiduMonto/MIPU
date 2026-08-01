# Guía de prototipo y diseño para MIPU

Este documento recoge el contexto actual del proyecto y una ruta práctica para el primer prototipo del smartwatch con XIAO nRF52840, Sharp Memory LCD, batería LiPo y tres botones.

## 1. Estado actual del proyecto

Archivos principales revisados:

- [CMakeLists.txt](CMakeLists.txt)
- [prj.conf](prj.conf)
- [xiao_ble.overlay](xiao_ble.overlay)
- [src/main.c](src/main.c)

Lo que ya está bien encaminado:

- El proyecto está montado como aplicación Zephyr normal, con `src/main.c` como punto de entrada.
- El overlay ya define el display `ls0xx`, el bus SPI2, la batería por divisor resistivo y un nodo para `cdc_acm_uart0` sobre `zephyr_udc0`.
- `prj.conf` ya contempla GPIO, SPI, ADC, Bluetooth, Date Time, LVGL, logging y USB.

Lo que conviene ajustar o tener muy presente:

- `src/main.c` necesitaba incluir `zephyr/logging/log.h` y `time.h` para que `LOG_MODULE_REGISTER` y `struct tm` estén bien definidos.
- Conviene declarar explícitamente `CONFIG_BT_NRF_SERVICES=y` junto con `CONFIG_BT_CTS_CLIENT=y` para dejar clara la dependencia del cliente CTS.
- El entorno de build aquí no tiene `ninja` instalado, así que no pude validar una compilación completa desde esta máquina.
- Ahora mismo el firmware todavía no tiene una capa real de UI ni una capa de entrada por botones; solo arranca USB y la sincronización Bluetooth de hora.

## 2. Objetivo del primer prototipo

La mejor estrategia para esta primera versión es mantener el firmware pequeño y muy modular. El objetivo no es tener todas las funciones finales, sino una base sólida que luego puedas ampliar sin reescribirlo todo.

### Pantallas mínimas recomendadas

1. Pantalla principal de reloj.
   - Hora grande.
   - Fecha debajo.
   - Estado de batería.
   - Estado Bluetooth/sincronización.
   - Un icono o texto pequeño de modo actual.

2. Menú principal.
   - Entrada a reloj, ajustes y pantallas futuras.
   - Navegación simple con 3 botones.

3. Pantalla de ajustes.
   - Sincronizar hora.
   - Estado del dispositivo.
   - Brillo o comportamiento de pantalla cuando el hardware lo permita.

4. Pantallas placeholder.
   - Estructuras vacías para futuras funciones.
   - Ejemplo: notificaciones, pasos, temporizador, batería detallada.

### Mapeo provisional de botones

Con tu respuesta, el teclado simulará los tres botones con `1`, `2` y `3`.

Recomendación de uso:

- `1` = arriba o anterior.
- `2` = aceptar o botón central.
- `3` = abajo o siguiente.

Si más adelante quieres, también puedes simular pulsación corta y larga con combinaciones como `Shift+1`, pero al principio no hace falta.

## 3. Arquitectura recomendada del firmware

La idea es separar responsabilidades desde ya, aunque el primer prototipo sea pequeño.

### Capas sugeridas

- `app/` o `src/app/` para la lógica principal de la aplicación.
- `ui/` para pantallas, navegación y renderizado.
- `input/` para botones físicos y simulación por teclado/USB console.
- `sensors/` para batería y futuras mediciones.
- `services/` para Bluetooth, sincronización de hora y cualquier comunicación.
- `drivers/` o integración directa para LCD, si luego decides encapsular más.

### Estado de la aplicación

Te conviene manejar un estado global pequeño, por ejemplo:

- `APP_SCREEN_HOME`
- `APP_SCREEN_MENU`
- `APP_SCREEN_SETTINGS`
- `APP_SCREEN_PLACEHOLDER_X`

Y un segundo estado para el sistema:

- `BT disconnected`
- `BT connected`
- `time synced`
- `battery charging` / `battery discharging`

Esto te permitirá mostrar información sin mezclar la lógica de negocio con el dibujo de la pantalla.

## 4. Cómo avanzar antes de tener hardware

### Fase 1: base funcional mínima

Haz que el firmware pueda arrancar y reportar claramente:

- USB consola activa.
- Bluetooth inicializado.
- Estado de sincronización de hora.
- Estado de batería simulado.
- Cambio entre pantallas desde teclado.

### Fase 2: UI de prototipo

Cuando tengas un primer esqueleto de pantalla:

- Dibuja la pantalla de reloj con datos fijos o simulados.
- Añade navegación por menús.
- Deja una zona reservada para iconos de batería y Bluetooth.
- Mantén textos cortos y legibles en una pantalla monocroma.

### Fase 3: integración real de hardware

Cuando llegue el LCD y la batería:

- Sustituye datos simulados por lectura real del divisor resistivo.
- Ajusta el refresco para no castigar la batería.
- Añade control de VCOM o el mecanismo que recomiende el driver del Sharp Memory LCD.
- Define un flujo de bajo consumo y suspensión.

### Fase 4: sincronización de hora usable

- Empareja con el teléfono.
- Pide la hora al conectar o al desbloquear la pantalla.
- Reintenta sincronización en intervalos largos, no constantemente.
- Guarda un fallback local si el teléfono no responde.

## 5. Diseño de pantallas

### Pantalla principal

Debe ser la más importante del sistema. Si la pantalla entra en modo reposo o se actualiza poco, esta sigue siendo la vista principal.

Contenido recomendado:

- Hora grande en el centro o parte superior.
- Fecha o día de la semana debajo.
- Icono de batería en una esquina.
- Indicador BT en otra esquina.
- Texto de evento breve abajo si hace falta.

Reglas prácticas:

- Prioriza legibilidad por encima de decoración.
- Usa pocas zonas visuales, no muchos bloques pequeños.
- En monocromo, el contraste y el espacio en blanco valen más que los efectos.

### Menú principal

Hazlo simple, tipo lista vertical.

Ejemplo:

- Hora
- Ajustes
- Bluetooth
- Estado del sistema
- Placeholder para futuro

La selección debe verse muy clara: inversión, flecha o bloque sólido.

### Ajustes

Empieza con solo unas pocas opciones útiles:

- Sincronizar hora.
- Ver nivel de batería.
- Ver versión del firmware.
- Volver al inicio.

## 6. Estrategia de entrada por teclado mientras no hay botones

Como todavía no tienes el hardware físico, el firmware debería permitir navegación con teclado desde la consola/USB serial.

Te recomiendo esta lógica:

- Leer entrada serial de forma no bloqueante o con un hilo dedicado.
- Traducir `1`, `2` y `3` a eventos de botón.
- Mantener la misma interfaz interna para botones físicos y simulados.

Así, cuando llegue el hardware, solo cambias la capa de entrada y el resto del sistema sigue igual.

## 7. Propuesta de estructura de código

Si el proyecto crece un poco, una estructura sana podría ser:

- `src/main.c` para el arranque.
- `src/app_state.c` y `src/app_state.h` para estado general.
- `src/ui/` para navegación y renderizado.
- `src/input/keyboard_buttons.c` para simulación.
- `src/input/gpio_buttons.c` para botones físicos.
- `src/services/time_sync.c` para Bluetooth CTS.
- `src/services/battery.c` para lectura de batería.

No hace falta crear todo esto hoy, pero sí conviene diseñarlo así desde el principio.

## 8. Recomendaciones técnicas concretas

- Usa un solo lenguaje de interfaz para toda la navegación de pantallas.
- Evita meter lógica de Bluetooth dentro del código de pantalla.
- Evita mezclar lectura de botones con dibujo de UI.
- Mantén la pantalla principal muy barata de refrescar.
- En el Sharp Memory LCD, actualiza solo lo necesario siempre que puedas.
- Piensa desde ya en suspensión, aunque al principio no la implementes.

## 9. Energía y batería

La batería va a definir mucho la experiencia final, aunque al principio parezca secundaria.

Sugerencias:

- Añade una lectura de batería simulada ahora para diseñar la interfaz.
- Luego conecta el ADC real al divisor resistivo.
- Define umbrales visuales:
  - batería alta
  - batería media
  - batería baja
  - batería crítica
- Reserva espacio para iconografía de carga si luego añades USB o carga activa.

## 10. Bluetooth y sincronización de hora

La sincronización de hora por CTS es una buena primera función porque da sensación de producto real desde el inicio.

Flujo recomendado:

1. Arranca Bluetooth.
2. Espera conexión del teléfono.
3. Solicita la hora cuando el enlace esté disponible.
4. Actualiza la hora interna del sistema.
5. Refresca la pantalla principal.

Más adelante puedes añadir reintentos, persistencia y una política de sincronización más inteligente.

## 11. Lo que haría inmediatamente después

1. Crear una pantalla principal falsa con hora estática o simulada.
2. Implementar navegación entre `Home` y `Menu` con `1/2/3`.
3. Separar la lógica de input, estado y UI en módulos distintos.
4. Añadir lectura de batería simulada.
5. Añadir luego la lectura real del ADC.
6. Conectar el LCD físico cuando llegue.

## 12. Riesgos a vigilar

- No mezclar demasiada lógica en `main.c`.
- No depender de refrescos demasiado frecuentes en el LCD.
- No dejar la sincronización de hora bloqueando el arranque.
- No diseñar menús muy profundos para una pantalla pequeña.
- No asumir que la lectura por teclado y la lectura por GPIO se implementarán de forma diferente internamente.

## 13. Preguntas abiertas que todavía conviene decidir

- Qué diseño visual quieres para el reloj: minimalista, técnico, retro o más moderno.
- Si la pantalla principal mostrará solo hora o también fecha y batería siempre.
- Si el menú será vertical, por tarjetas o por una estructura más tipo lista.
- Si los botones tendrán pulsación corta y larga desde el principio.
- Si la simulación por teclado debe vivir en la consola USB o en shell.
- Si quieres guardar la configuración en flash desde el primer prototipo.

## 14. Resumen corto

Tu base actual ya sirve como punto de arranque, pero aún necesita separar mejor la arquitectura. Mi recomendación es: primero fija una UI mínima con `Home` y `Menu`, simula botones con `1/2/3`, mantén Bluetooth como servicio independiente y deja el LCD como una capa aislada para poder sustituir simulación por hardware sin rehacer el firmware.