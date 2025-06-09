/// @file
/// @brief Presentamos el API de C para devolver los datos.
/// Los datos sobre el clima siempre son en grados celsius
/// \ image html server_api.png
#pragma once
#include <cjson/cJSON.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mongoose.h"

/// \ Puerto en el que se ejecuta el servidor.
#define PORT "8080"

/// \ Dirección de la base de datos dentro del entorno de Docker.
#define DB_PATH "/data/dev.db"

#define RETRY_DELAY 10
#define KEYS_JSON "/data/var_specs.json"

/// \ Datos del clima en paquete.
const char* c_get_weather_data(double lat, double lon, int* code);

char** build_keys(int* out_n);

/// \ Datos del clima por variable.
const char* c_get_weather_var(double lat, double lon, char* var, int* code);
