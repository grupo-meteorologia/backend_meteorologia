#include "parser.h"

double round_value(double value) { return round(value * 100.0) / 100.0; }

int call_build_var_specs() {
    fprintf(stderr, "[DEBUG] call_build_var_specs\n");
    char cmd[1024];

    snprintf(cmd, sizeof(cmd), "%s", VAR_SCRIPT);

    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "[parser] ERROR: build_var_specs.py devolvió código %d",
                ret);
    }
    return ret;
    return 0;
}

int call_parse_and_store(const char* timestamp, double lat, double lon,
                         int ready) {
    fprintf(stderr, "[DEBUG] call_parse_and_store\n");
    char cmd[1024];

    if (ready == 0)
        snprintf(cmd, sizeof(cmd), "%s \"%s\" %.2f %.2f", PARSE_SCRIPT,
                 timestamp, round_value(lat), round_value(lon));
    else
        snprintf(cmd, sizeof(cmd), "%s \"%s\" %.2f %.2f --ready", PARSE_SCRIPT,
                 timestamp, round_value(lat), round_value(lon));

    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr,
                "[parser] ERROR: parse_and_store.py devolvió código %d para "
                "%s,%.2f,%.2f\n",
                ret, timestamp, lat, lon);
    }
    return ret;
}
