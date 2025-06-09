#include "fetcher.h"

#include "constants.h"

CURL *master = NULL;

struct progress {
    curl_off_t now, total;
} progs[NUM_FILES];

struct cbdata {
    int idx;
};

int fetch_current_conditions(double lat, double lon, char *out_path,
                             size_t out_path_size) {
    fprintf(stderr, "[DEBUG] fetch_current_conditions\n");
    CURL *handle = curl_easy_init();
    if (!handle) return 1;

    char url[512];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/"
             "forecast?latitude=%.2f&longitude=%.2f&current_weather=true",
             lat, lon);

    snprintf(out_path, out_path_size, LIVE_DIR "%.2f_%.2f.json", lat, lon);
    FILE *fp = fopen(out_path, "w");
    if (!fp) {
        perror("fopen");
        return 1;
    }

    curl_easy_setopt(handle, CURLOPT_URL, url);
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, NULL);

    CURLcode res = curl_easy_perform(handle);
    fclose(fp);
    curl_easy_cleanup(handle);
    set_master();

    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform failed: %s\n",
                curl_easy_strerror(res));
        return 1;
    }

    return 0;
}

// void redraw_bars() {
//     fprintf(stderr, "\x1b[%dA", NUM_FILES);
//     for (int i = 0; i < NUM_FILES; i++) {
//         curl_off_t dlnow = progs[i].now;
//         curl_off_t dltotal = progs[i].total;
//
//         int pct = dltotal > 0 ? (int)(dlnow * 100 / dltotal) : 0;
//         int filled = pct * WIDTH / 100;
//
//         fprintf(stderr, "[%02d] [", i);
//         for (int j = 0; j < WIDTH; j++) fputc(j < filled ? '#' : ' ',
//         stderr); fprintf(stderr, "] %3d%%\n", pct);
//     }
//     fflush(stdout);
// }
//
// int progress_cb(void *p, curl_off_t dltotal, curl_off_t dlnow,
//                 curl_off_t ultotal, curl_off_t ulnow) {
//     fprintf(stderr, "[DEBUG] progress_cb\n");
//     struct cbdata *d = p;
//     progs[d->idx].now = dlnow;
//     progs[d->idx].total = dltotal;
//
//     redraw_bars();
//
//     return 0;
// }

int find_latest_run(struct tm *out) {
    fprintf(stderr, "[DEBUG] find_latest_run\n");
    time_t now = time(NULL);

    for (int i = 0; i < 4; i++) {
        set_master();

        time_t cand = now - (time_t)i * 6 * 3600;
        struct tm tm_cand;
        gmtime_r(&cand, &tm_cand);
        tm_cand.tm_hour -= tm_cand.tm_hour % 6;

        int year = tm_cand.tm_year + 1900, mon = tm_cand.tm_mon + 1,
            day = tm_cand.tm_mday, hour = tm_cand.tm_hour;

        char url[MAX_URL] = {0};

        snprintf(url, sizeof(url),
                 "https://s3.us-west-2.amazonaws.com/smn-ar-wrf/"
                 "DATA/WRF/DET/%04d/%02d/%02d/%02d/"
                 "WRFDETAR_10M_%04d%02d%02d_%02d_000.nc",
                 year, mon, day, hour, year, mon, day, hour);

        curl_easy_setopt(master, CURLOPT_URL, url);
        curl_easy_setopt(master, CURLOPT_NOBODY, 1L);
        curl_easy_setopt(master, CURLOPT_FOLLOWLOCATION, 1L);

        int res = curl_easy_perform(master);
        if (res != CURLE_OK) {
            fprintf(stderr,
                    "[ERROR] curl_easy_perform(...) failed for \"%s\"\n"
                    "        CURLcode = %d (%s)\n",
                    url, (int)res, curl_easy_strerror(res));
            set_master();
            return 1;
        }
        long code = 0;

        curl_easy_getinfo(master, CURLINFO_RESPONSE_CODE, &code);
        if (code == 200) {
            fprintf(stderr, "[DEBUG] curl_easy_getinfo\n");
            *out = tm_cand;
            set_master();
            return 0;
        }
    }
    fprintf(stderr, "[ERROR] More than 24 hours, line 116\n");
    set_master();
    return 1;
}

void generate_keys(const struct tm *run, int offset[NUM_FILES],
                   char keys[NUM_FILES][MAX_URL]) {
    fprintf(stderr, "[DEBUG] generate_keys\n");
    const char *templates[NUM_FILES] = {
        "DATA/WRF/DET/%04d/%02d/%02d/%02d/"
        "WRFDETAR_10M_%04d%02d%02d_%02d_%03d.nc",
        "DATA/WRF/DET/%04d/%02d/%02d/%02d/"
        "WRFDETAR_01H_%04d%02d%02d_%02d_%03d.nc",
        "DATA/WRF/DET/%04d/%02d/%02d/%02d/"
        "WRFDETAR_24H_%04d%02d%02d_%02d_%03d.nc",
    };

    int year = run->tm_year + 1900, mon = run->tm_mon + 1, day = run->tm_mday,
        hour = run->tm_hour;

    for (int i = 0; i < NUM_FILES; i++) {
        snprintf(keys[i], MAX_URL, templates[i], year, mon, day, hour, year,
                 mon, day, hour, offset[i]);
    }
}

bool run_already_downloaded(struct tm *run_tm) {
    fprintf(stderr, "[DEBUG] run_already_downloaded\n");
    char path[MAX_URL];
    snprintf(path, sizeof(path),
             RAW_DIR "WRFDETAR_10M_%04d%02d%02d_%02d_000.nc",
             run_tm->tm_year + 1900, run_tm->tm_mon + 1, run_tm->tm_mday,
             run_tm->tm_hour);

    return access(path, F_OK) == 0;
}

int cpy_into_permanent(const char outpaths[NUM_FILES][MAX_URL]) {
    char perm_path[MAX_URL];
    char buf[8192];

    for (int i = 0; i < NUM_FILES; i++) {
        const char *tmp_path = outpaths[i];
        if (tmp_path[0] == '\0') continue;

        const char *base_name = strrchr(tmp_path, '/');
        base_name = base_name ? base_name + 1 : tmp_path;

        if (snprintf(perm_path, sizeof perm_path, "%s%s", RAW_DIR, base_name) >=
            (int)sizeof perm_path) {
            return 1;
        }

        if (rename(tmp_path, perm_path) == 0) {
            continue;
        }

        FILE *src = fopen(tmp_path, "rb");
        if (!src) return 1;
        FILE *dst = fopen(perm_path, "wb");
        if (!dst) {
            fclose(src);
            return 1;
        }

        size_t r;
        while ((r = fread(buf, 1, sizeof buf, src)) > 0) {
            if (fwrite(buf, 1, r, dst) != r) {
                fclose(src);
                fclose(dst);
                return 1;
            }
        }

        fclose(src);
        fclose(dst);

        if (remove(tmp_path) != 0) {
            return 1;
        }
    }
    return 0;
}

int download_parallel(const char keys[NUM_FILES][MAX_URL]) {
    fprintf(stderr, "[DEBUG] download_parallel\n");
    CURLM *multi = curl_multi_init();
    if (!multi) return 1;

    CURL *handles[NUM_FILES] = {0};
    FILE *files[NUM_FILES] = {0};

    char url[NUM_FILES][MAX_URL] = {{0}}, outpath[NUM_FILES][MAX_URL] = {{0}};

    for (int i = 0; i < NUM_FILES; i++) {
        const char *fn =
            construct_file_name((char *)keys[i], url[i], outpath[i]);

        void *st_unsafe = check_time(&files[i], outpath[i]);
        if (st_unsafe == NULL) {
            fprintf(stderr, "[DEBUG] check_time\n");
            continue;
        }
        struct stat *st = st_unsafe;

        handles[i] = curl_easy_duphandle(master);
        curl_easy_setopt(handles[i], CURLOPT_URL, url[i]);
        curl_easy_setopt(handles[i], CURLOPT_WRITEFUNCTION, NULL);
        curl_easy_setopt(handles[i], CURLOPT_WRITEDATA, files[i]);

        if (st->st_mtime != 0) {
            curl_easy_setopt(handles[i], CURLOPT_TIMECONDITION,
                             CURL_TIMECOND_IFMODSINCE);
            curl_easy_setopt(handles[i], CURLOPT_TIMEVALUE, (long)st->st_mtime);
        }
        free(st);

        struct cbdata *d = malloc(sizeof *d);
        d->idx = i;
        // curl_easy_setopt(handles[i], CURLOPT_XFERINFOFUNCTION,
        // progress_cb);
        curl_easy_setopt(handles[i], CURLOPT_XFERINFODATA, d);
        curl_easy_setopt(handles[i], CURLOPT_NOPROGRESS, 0L);

        curl_multi_add_handle(multi, handles[i]);

        progs[i].now = progs[i].total = 0;
    }
    fflush(stderr);

    multi_perform(multi);

    for (int i = 0; i < NUM_FILES; i++) {
        if (!handles[i]) continue;
        if (check_response(handles[i], outpath[i], files[i])) continue;

        curl_multi_remove_handle(multi, handles[i]);
        curl_easy_cleanup(handles[i]);
    }

    curl_multi_cleanup(multi);

    if (cpy_into_permanent(outpath) != 0) {
        fprintf(stderr, "[ERROR] cpy_into_permanent failed\n");
        return 1;
    }

    return 0;
}

const char *construct_file_name(const char *key, char url[MAX_URL],
                                char outpath[MAX_URL]) {
    fprintf(stderr, "[DEBUG] *construct_file_name\n");
    const char *fn = strrchr(key, '/');
    fn = fn ? fn + 1 : key;
    snprintf(url, MAX_URL, "https://smn-ar-wrf.s3.us-west-2.amazonaws.com/%s",
             key);
    snprintf(outpath, MAX_URL, "%s%s", TEMP_DIR, fn);
    fprintf(stderr, "[DEBUG] constructed file name: %s\n", fn);

    return fn;
}

void *check_time(FILE **file, char outpath[MAX_URL]) {
    fprintf(stderr, "[DEBUG] *check_time\n");
    struct stat *st = malloc(sizeof(struct stat));
    if (stat(outpath, st) != 0) st->st_mtime = 0;

    fprintf(stderr, "[DEBUG] check_time(): opening file → '%s'\n", outpath);

    *file = fopen(outpath, "wb");
    if (!*file) {
        perror("outpath");
        free(st);
        return NULL;
    }
    return st;
}

void multi_perform(CURLM *multi) {
    fprintf(stderr, "[DEBUG] multi_perform\n");
    int still_running = 0;

    curl_multi_perform(multi, &still_running);

    while (still_running) {
        curl_multi_wait(multi, NULL, 0, 1000, NULL);
        curl_multi_perform(multi, &still_running);
    }
}

int check_response(CURL *handle, char outpath[MAX_URL], FILE *file) {
    fprintf(stderr, "[DEBUG] check_response\n");
    if (!handle) return 1;
    long code = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &code);

    fclose(file);
    if (code == 200) {
        fprintf(stderr, "[DEBUG] Downloaded %s (HTTP %ld)\n", outpath, code);
        return 0;
    } else if (code == 304) {
        fprintf(stderr, "[DEBUG] %s not modified. Skipped.\n", outpath);
        return 0;
    } else {
        fprintf(stderr, "[ERROR] Código distinto a 200, %s (HTTP %ld)\n",
                outpath, code);
    }
    return 1;
}

void set_master() {
    if (!master) return;
    curl_easy_reset(master);

    curl_easy_setopt(master, CURLOPT_NOBODY, 0L);
    curl_easy_setopt(master, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(master, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(master, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(master, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    curl_easy_setopt(master, CURLOPT_SSL_SESSIONID_CACHE, 1L);
    curl_easy_setopt(master, CURLOPT_DNS_CACHE_TIMEOUT, 600L);
    curl_easy_setopt(master, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(master, CURLOPT_TCP_KEEPIDLE, 120L);
    curl_easy_setopt(master, CURLOPT_TCP_KEEPINTVL, 60L);
}
