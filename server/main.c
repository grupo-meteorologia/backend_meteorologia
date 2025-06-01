/// @file
/// @brief Presentamos el API de C para devolver los datos.
/// Los datos sobre el clima siempre son en grados celsius

#include <stdio.h>
#include <stdlib.h>

#include "./server.h"

static void send_json(struct mg_connection* c, const char* json) {
    mg_printf(c,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: application/json; charset=utf-8\r\n"
              "Content-Length: %zu\r\n\r\n"
              "%s",
              strlen(json), json);
}

static void handle_request(struct mg_connection* c, int ev, void* ev_data) {
    if (ev != MG_EV_HTTP_MSG) return;
    struct mg_http_message* hm = (struct mg_http_message*)ev_data;
    char lat_buf[32] = {0};
    char lon_buf[32] = {0};
    char type[64] = {0};
    const char* response;

    mg_http_get_var(&hm->query, "lat", lat_buf, sizeof(lat_buf));
    mg_http_get_var(&hm->query, "lon", lon_buf, sizeof(lon_buf));
    mg_http_get_var(&hm->query, "type", type, sizeof(type));

    if (strlen(lat_buf) == 0 || strlen(lon_buf) == 0) {
        send_json(c, "{\"error\": \"Falta el parámetro 'lat' y/o 'lon'\"} ");
        return;
    }

    double lat = atof(lat_buf);
    double lon = atof(lon_buf);

    if (strcmp(type, "temperature") == 0) {
        response = c_get_temperature(lat, lon);
    } else if (strcmp(type, "humidity") == 0) {
        response = c_get_humidity(lat, lon);
    } else if (strcmp(type, "wind") == 0) {
        response = c_get_wind_speed(lat, lon);
    } else if (strcmp(type, "forecast") == 0) {
        response = c_get_forecast(lat, lon);
    } else {
        response = c_get_weather_data(lat, lon);
    }

    send_json(c, response);
}

/// Datos del clima
const char* c_get_weather_data(double lat, double lon) {}

/// Meteorologo semanal
const char* c_get_forecast(double lat, double lon) {}

/// Obtener Temperatura
const char* c_get_temperature(double lat, double lon) {
    static char path[128];
    snprintf(path, sizeof(path), "data/json/%.2f_%.2f.json", lat, lon);

    FILE* f = fopen(path, "r");
    if (!f) return "{\'error\': \"No hay datos para esa ubicación\"}";

    static char json[4096];
    fread(json, 1, sizeof(json) - 1, f);
    fclose(f);
    return json;
}

/// Obtener humedad
const char* c_get_humidity(double lat, double lon) {}

/// Obtener viento
const char* c_get_wind_speed(double lat, double lon) {}

int main(void) {
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    mg_http_listen(&mgr, "http://localhost:8000", handle_request, NULL);
    printf("Listening to localhost 8000");

    while (1) {
        mg_mgr_poll(&mgr, 1000);
    }

    mg_mgr_free(&mgr);
    return 0;
}
