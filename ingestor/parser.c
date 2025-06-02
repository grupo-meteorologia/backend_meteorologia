#include "parser.h"

#include <sqlite3.h>
#include <stdio.h>

double round_value(double value) { return round(value * 100.0) / 100.0; }

int call_parse_and_store(const char* timestamp, double lat, double lon) {
    fprintf(stderr, "[DEBUG] call_parse_and_store");
    char cmd[1024];

    snprintf(cmd, sizeof(cmd), "%s %s %.2f %.2f", PARSE_SCRIPT, timestamp, lon,
             lat);

    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr,
                "[parser] ERROR: parse_and_store.py devolvió código %d para "
                "%s,%.2f,%.2f\n",
                ret, timestamp, lat, lon);
    }
    return ret;
}
