/* The pin map of the board this firmware is built for.
 *
 * THE board_pins_<board>.h FILES ARE THE ONLY PLACE IN THIS PROJECT THAT MAY
 * CONTAIN GPIO NUMBERS. Each defines the same BOARD_* names; code that differs
 * per board asks a BOARD_HAS_* feature, not which board it is. The board is
 * chosen at build time: `idf.py -D VFO_BOARD=...`, see CMakeLists.txt.
 */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#if VFO_BOARD_LCD185C
#include "board_pins_lcd185c.h"
#elif VFO_BOARD_KNOB18
#include "board_pins_knob18.h"
#else
#error "no board selected: build with -D VFO_BOARD=<board>"
#endif

#endif /* BOARD_PINS_H */
