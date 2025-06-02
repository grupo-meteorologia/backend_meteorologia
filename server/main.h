#pragma once
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mongoose.h"

#define PORT "8080"
#define DB_PATH "/data/dev.db"

const char* c_get_weather_data(double lat, double lon);
const char* c_get_forecast(double lat, double lon);
const char* c_get_weather_var(double lat, double lon, char* var);
