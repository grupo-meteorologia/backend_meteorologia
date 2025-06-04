#include "./main.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>

#include "mongoose.h"

static const char* KEYS[] = {"temperature",    "humidity", "wind_speed",
                             "wind_direction", "pressure", "temp_max",
                             "temp_min"};
static const int NUM_KEYS = sizeof(KEYS) / sizeof(KEYS[0]);

static void send_json(struct mg_connection* c, const char* json,
                      int status_code) {
    fprintf(stderr, "[DEBUG] Entered send_json with: %s\n", json);
    if (!c) {
        fprintf(stderr, "[ERROR] mg_connection is NULL in send_json()\n");
        return;
    }
    mg_http_reply(c, status_code, "Content-Type: application/json\r\n", "%s",
                  json);
    fprintf(stderr, "[DEBUG] Sent json\n");
}

static void handle_request(struct mg_connection* c, int ev, void* ev_data) {
    if (!ev_data) {
        fprintf(stderr, "[ERROR] ev_data is NULL\n");
        return;
    }
    if (ev != MG_EV_HTTP_MSG) return;

    fprintf(stderr, "[DEBUG] request received\n");

    struct mg_http_message* hm = (struct mg_http_message*)ev_data;
    fprintf(stderr, "[DEBUG] ev_data ptr: %p\n", ev_data);
    if (!hm->query.buf) {
        fprintf(stderr, "[DEBUG] hm->query.buf is NULL\n");
        send_json(c, "{\"error\": \"Faltan parámetros en la URL\"}", 400);
        return;
    } else {
        fprintf(stderr, "[DEBUG] hm->query.buf: %.*s\n", (int)hm->query.len,
                hm->query.buf);
    }

    char lat_buf[32] = {0};
    char lon_buf[32] = {0};
    char type[64] = {0};
    const char* response;

    mg_http_get_var(&hm->query, "lat", lat_buf, sizeof(lat_buf));
    mg_http_get_var(&hm->query, "lon", lon_buf, sizeof(lon_buf));
    mg_http_get_var(&hm->query, "type", type, sizeof(type));

    if (strlen(lat_buf) == 0 || strlen(lon_buf) == 0) {
        send_json(c, "{\"error\": \"Falta el parámetro 'lat' y/o 'lon'\"}",
                  400);
        return;
    }

    char* endptr_lat;
    char* endptr_lon;

    double lat = strtod(lat_buf, &endptr_lat);
    double lon = strtod(lon_buf, &endptr_lon);

    if (*endptr_lat != '\0' || *endptr_lon != '\0') {
        send_json(
            c,
            "{\"error\": \"Los parámetros 'lat' y 'lon' deben ser números\"}",
            400);
        return;
    }

    int code = 0;
    if (strlen(type) > 0) {
        fprintf(stderr, "[DEBUG] Entering c_get_weather_var\n");
        response = c_get_weather_var(lat, lon, type, &code);
    } else {
        fprintf(stderr, "[DEBUG] Entering c_get_weather_data\n");
        response = c_get_weather_data(lat, lon, &code);
    }

    send_json(c, response, code);
}

int insert_into_missing(double lat, double lon, sqlite3* db) {
    const char* query =
        "INSERT OR IGNORE INTO missing_requests (lat, lon) VALUES (?, ?);";

    sqlite3_stmt* stmt = NULL;

    if (sqlite3_prepare_v3(db, query, -1, 0, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "Couldn't prepare db\n");
        return 1;
    }

    sqlite3_bind_double(stmt, 1, lat);
    sqlite3_bind_double(stmt, 2, lon);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return 0;
}

const char* c_get_weather_data(double lat, double lon, int* code) {
    static char json[512];

    sqlite3* db = NULL;
    sqlite3_stmt* stmt = NULL;

    const char* query =
        "SELECT value FROM weather WHERE variable = ? AND "
        "ABS(lat-?) < 0.01 AND ABS(lon-?) < 0.01 ORDER BY timestamp DESC LIMIT "
        "1;";

    if (sqlite3_open(DB_PATH, &db) != SQLITE_OK) {
        snprintf(json, sizeof(json),
                 "{\"error\":\"No se pudo abrir la base de datos\"}");
        return json;
    }

    int pos = 0;
    pos += snprintf(json + pos, sizeof(json) - pos, "{");

    int found_any = 0;
    for (int i = 0; i < NUM_KEYS; i++) {
        const char* var = KEYS[i];
        double value = 0.0;

        if (sqlite3_prepare_v3(db, query, -1, 0, &stmt, NULL) != SQLITE_OK) {
            sqlite3_close(db);
            *code = 500;
            snprintf(json, sizeof(json),
                     "{\"error\": \"Error al preparar la consulta interna\"}");
            return json;
        }

        sqlite3_bind_text(stmt, 1, var, -1, SQLITE_STATIC);
        sqlite3_bind_double(stmt, 2, lat);
        sqlite3_bind_double(stmt, 3, lon);

        if (sqlite3_step(stmt) != SQLITE_ROW) {
            *code = 202;
            fprintf(stderr, "No hay datos en db para lat %.2f y lon %.2f\n",
                    lat, lon);
            insert_into_missing(lat, lon, db);
            sqlite3_finalize(stmt);
            continue;
        }

        value = sqlite3_column_double(stmt, 0);

        sqlite3_finalize(stmt);

        if (found_any) {
            pos += snprintf(json + pos, sizeof(json) - pos, ", ");
        }
        pos += snprintf(json + pos, sizeof(json) - pos, "\"%s\": %.2f", var,
                        value);

        found_any = 1;
    }

    sqlite3_close(db);

    if (!found_any) {
        *code = 202;
        snprintf(json, sizeof(json),
                 "{\"error\":\"No hay datos para esa ubicación\"}");
        return json;
    }

    pos += snprintf(json + pos, sizeof(json) - pos, "}");
    if (*code == 0) *code = 200;
    return json;
}

const char* c_get_weather_var(double lat, double lon, char* var, int* code) {
    static char json[128];
    sqlite3* db = NULL;
    sqlite3_stmt* stmt = NULL;

    const char* query =
        "SELECT value FROM weather WHERE variable = ? AND "
        "ABS(lat-?) < 0.01 AND ABS(lon-?) < 0.01 ORDER BY timestamp DESC LIMIT "
        "1;";

    if (sqlite3_open(DB_PATH, &db) != SQLITE_OK) {
        return "{\"ERROR\": \"No se pudo abrir la base de datos.\"}";
    }

    if (sqlite3_prepare_v3(db, query, -1, 0, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return "{\"ERROR\": \"Error al preparar la consulta.\"}";
    }

    sqlite3_bind_text(stmt, 1, var, strlen(var), SQLITE_STATIC);
    sqlite3_bind_double(stmt, 2, lat);
    sqlite3_bind_double(stmt, 3, lon);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        *code = 202;
        snprintf(json, sizeof(json),
                 "{\"error\":\"No hay datos de %s para lat %.2f y lon %.2f\"}",
                 var, lat, lon);
        insert_into_missing(lat, lon, db);
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return json;
    }

    double val = sqlite3_column_double(stmt, 0);
    snprintf(json, sizeof(json), "{\"%s\": %.2f}", var, val);

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    if (*code == 0) *code = 200;
    return json;
}

int main(void) {
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    struct mg_connection* c =
        mg_http_listen(&mgr, "http://0.0.0.0:" PORT, handle_request, NULL);

    if (!c) {
        fprintf(stderr, "ERROR: Cannot bind to port %s\n", PORT);
        return 1;
    }

    fprintf(stderr, "Listening on port %s\n", PORT);

    for (;;) {
        mg_mgr_poll(&mgr, 1000);
    }

    mg_mgr_free(&mgr);
    return 0;
}
