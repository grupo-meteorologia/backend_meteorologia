#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mongoose.h"

#define PORT 8080

const char* c_get_weather_data(double lat, double lon);
const char* c_get_forecast(double lat, double lon);
const char* c_get_temperature(double lat, double lon);
const char* c_get_humidity(double lat, double lon);
const char* c_get_wind_speed(double lat, double lon);
