/// @file
/// @brief Procesamos y poblamos la base de datos.
#pragma once

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"

/// Función en c que llama al script parse_and_store.py para una posición y un
/// momento dado.
int call_parse_and_store(const char* timestamp, double lat, double lon);
