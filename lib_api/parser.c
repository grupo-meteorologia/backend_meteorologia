#include <cjson/cJSON.h>
#include <dirent.h>
#include <netcdf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define RAW_DIR "data/raw"
#define JSON_DIR "data/json"

#define NC_CHECK(ERR)                                            \
    if ((ERR) != NC_NOERR) {                                     \
        fprintf(stderr, "NetCDF error: %s\n", nc_strerror(ERR)); \
        return;                                                  \
    }

void convert_nc_to_json(const char *inpath, const char *outpath) {
    int ncid, ndims, nvars, ngatts, unlimdimid;
    int retval;

    /* Open the NetCDF file read‐only */
    if ((retval = nc_open(inpath, NC_NOWRITE, &ncid))) {
        fprintf(stderr, "Failed to open %s: %s\n", inpath, nc_strerror(retval));
        return;
    }

    /* Inquire how many dims, vars, atts */
    NC_CHECK(nc_inq(ncid, &ndims, &nvars, &ngatts, &unlimdimid));

    /* Root JSON object */
    cJSON *root = cJSON_CreateObject();

    /* === 1) Dimensions === */
    cJSON *dims_arr = cJSON_CreateArray();
    for (int d = 0; d < ndims; d++) {
        char name[NC_MAX_NAME + 1];
        size_t len;
        NC_CHECK(nc_inq_dim(ncid, d, name, &len));

        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "name", name);
        cJSON_AddNumberToObject(obj, "length", (double)len);
        cJSON_AddItemToArray(dims_arr, obj);
    }
    cJSON_AddItemToObject(root, "dimensions", dims_arr);

    /* === 2) Global attributes === */
    cJSON *gatt_arr = cJSON_CreateArray();
    for (int a = 0; a < ngatts; a++) {
        char attname[NC_MAX_NAME + 1];
        nc_type att_type;
        size_t att_len;

        NC_CHECK(nc_inq_attname(ncid, NC_GLOBAL, a, attname));
        NC_CHECK(nc_inq_att(ncid, NC_GLOBAL, attname, &att_type, &att_len));

        cJSON *att_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(att_obj, "name", attname);

        /* Only handle text and numbers simply */
        if (att_type == NC_CHAR) {
            char *buf = malloc(att_len + 1);
            NC_CHECK(nc_get_att_text(ncid, NC_GLOBAL, attname, buf));
            buf[att_len] = '\0';
            cJSON_AddStringToObject(att_obj, "value", buf);
            free(buf);
        } else {
            /* read first element as number */
            double v;
            NC_CHECK(nc_get_att_double(ncid, NC_GLOBAL, attname, &v));
            cJSON_AddNumberToObject(att_obj, "value", v);
        }
        cJSON_AddItemToArray(gatt_arr, att_obj);
    }
    cJSON_AddItemToObject(root, "global_attributes", gatt_arr);

    /* === 3) Variables === */
    cJSON *vars_arr = cJSON_CreateArray();
    for (int v = 0; v < nvars; v++) {
        char vname[NC_MAX_NAME + 1];
        nc_type vtype;
        int vndims, natt;
        int dimids[NC_MAX_VAR_DIMS];

        NC_CHECK(nc_inq_var(ncid, v, vname, &vtype, &vndims, dimids, &natt));

        cJSON *vobj = cJSON_CreateObject();
        cJSON_AddStringToObject(vobj, "name", vname);

        /* --- variable dimensions lengths --- */
        cJSON *vdims = cJSON_CreateArray();
        size_t total_elems = 1;
        for (int j = 0; j < vndims; j++) {
            size_t dlen;
            NC_CHECK(nc_inq_dimlen(ncid, dimids[j], &dlen));
            total_elems *= dlen;
            cJSON_AddItemToArray(vdims, cJSON_CreateNumber((double)dlen));
        }
        cJSON_AddItemToObject(vobj, "shape", vdims);

        /* --- read and flatten data --- */
        if (total_elems > 0) {
            /* we'll read everything as double */
            double *data = malloc(sizeof(double) * total_elems);
            if (!data) {
                fprintf(stderr, "OOM allocating data buffer\n");
                cJSON_Delete(root);
                nc_close(ncid);
                return;
            }

            /* pick the right getter */
            switch (vtype) {
                case NC_DOUBLE:
                    NC_CHECK(nc_get_var_double(ncid, v, data));
                    break;
                case NC_FLOAT: {
                    float *tmp = malloc(sizeof(float) * total_elems);
                    NC_CHECK(nc_get_var_float(ncid, v, tmp));
                    for (size_t i = 0; i < total_elems; i++) data[i] = tmp[i];
                    free(tmp);
                } break;
                case NC_INT: {
                    int *tmp = malloc(sizeof(int) * total_elems);
                    NC_CHECK(nc_get_var_int(ncid, v, tmp));
                    for (size_t i = 0; i < total_elems; i++) data[i] = tmp[i];
                    free(tmp);
                } break;
                default:
                    /* unsupported types skip data */
                    memset(data, 0, sizeof(double) * total_elems);
                    break;
            }

            /* build JSON array */
            cJSON *darr = cJSON_CreateArray();
            for (size_t i = 0; i < total_elems; i++)
                cJSON_AddItemToArray(darr, cJSON_CreateNumber(data[i]));
            cJSON_AddItemToObject(vobj, "data", darr);

            cJSON_AddItemToArray(vars_arr, vobj);

            free(data);
        }
    }
    cJSON_AddItemToObject(root, "variables", vars_arr);

    /* === 4) Write JSON to file === */
    char *outstr = cJSON_Print(root);
    FILE *fp = fopen(outpath, "w");
    if (!fp) {
        perror("fopen");
    } else {
        fputs(outstr, fp);
        fclose(fp);
    }
    free(outstr);
    cJSON_Delete(root);
    nc_close(ncid);
}

int main(void) {
    struct stat st = {0};
    if (stat(JSON_DIR, &st) == -1) {
        mkdir(JSON_DIR, 0755);
    }

    DIR *d = opendir(RAW_DIR);
    if (!d) {
        perror("opendir data/raw");
        return 1;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        size_t len = strlen(ent->d_name);
        if (len > 3 && strcmp(ent->d_name + len - 3, ".nc") == 0) {
            char inpath[512], outpath[512];
            snprintf(inpath, sizeof(inpath), "%s/%s", RAW_DIR, ent->d_name);

            /* replace .nc with .json */
            char *dot = strrchr(ent->d_name, '.');
            snprintf(outpath, sizeof(outpath), "%s/%.*s.json", JSON_DIR,
                     (int)(dot - ent->d_name), ent->d_name);

            printf("Converting %s → %s\n", inpath, outpath);
            convert_nc_to_json(inpath, outpath);
        }
    }
    closedir(d);
    return 0;
}
