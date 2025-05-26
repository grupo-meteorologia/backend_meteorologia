#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RAW_DIR "data/raw/"
#define JSON_DIR "data/json"

FILE* nc_get_subset_as_json(char* inname, char* outname, float lon0, float lon1,
                            float lat0, float lat1) {
    char cmd[1024];
    int ret;
    snprintf(cmd, sizeof(cmd),
             "python3 subset_nc_to_json.py" RAW_DIR "%s" JSON_DIR
             "%s"
             "--lon-min %f --lon-max %f --lat-min %f --lat-max %f",
             inname, outname, lon0, lon1, lat0, lat1);
    ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "Error: subset script failed (code %d)\n", ret);
        return NULL;
    }

    char fullpath[512] = {0};
    snprintf(fullpath, sizeof(fullpath), JSON_DIR "%s", outname);
    FILE* fp = fopen(fullpath, "r");
    if (!fp) {
        perror("fopen json output");
        return NULL;
    }

    return fp;
}
