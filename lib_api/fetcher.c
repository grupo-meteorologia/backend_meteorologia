#include "./fetcher.h"

#include <curl/system.h>

static CURL *master = NULL;

struct progress {
    curl_off_t now, total;
} progs[NUM_FILES];

struct cbdata {
    int idx;
};

void redraw_bars() {
    printf("\x1b[%dA", NUM_FILES);
    for (int i = 0; i < NUM_FILES; i++) {
        curl_off_t dlnow = progs[i].now;
        curl_off_t dltotal = progs[i].total;

        int pct = dltotal > 0 ? (int)(dlnow * 100 / dltotal) : 0;
        int filled = pct * WIDTH / 100;

        fprintf(stderr, "[%02d] [", i);
        for (int j = 0; j < WIDTH; j++) fputc(j < filled ? '#' : ' ', stderr);
        fprintf(stderr, "] %3d%%\n", pct);
    }
    fflush(stdout);
}

static int progress_cb(void *p, curl_off_t dltotal, curl_off_t dlnow,
                       curl_off_t ultotal, curl_off_t ulnow) {
    struct cbdata *d = p;
    progs[d->idx].now = dlnow;
    progs[d->idx].total = dltotal;

    redraw_bars();

    return 0;
}

static int find_latest_run(struct tm *out) {
    time_t now = time(NULL);

    for (int i = 0; i < 4; i++) {
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

        if (curl_easy_perform(master) != CURLE_OK) {
            return 1;
        }
        long code = 0;

        curl_easy_getinfo(master, CURLINFO_RESPONSE_CODE, &code);
        if (code == 200) {
            *out = tm_cand;
            return 0;
        }
    }
    return 1;
}

static void generate_keys(const struct tm *run, int offset[NUM_FILES],
                          char keys[NUM_FILES][MAX_URL]) {
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

int download_parallel(const char keys[NUM_FILES][MAX_URL]) {
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
        curl_easy_setopt(handles[i], CURLOPT_XFERINFOFUNCTION, progress_cb);
        curl_easy_setopt(handles[i], CURLOPT_XFERINFODATA, d);
        curl_easy_setopt(handles[i], CURLOPT_NOPROGRESS, 0L);

        curl_multi_add_handle(multi, handles[i]);

        progs[i].now = progs[i].total = 0;
    }
    fflush(stdout);

    multi_perform(multi);

    for (int i = 0; i < NUM_FILES; i++) {
        if (check_response(handles[i], outpath[i], files[i])) continue;

        curl_multi_remove_handle(multi, handles[i]);
        curl_easy_cleanup(handles[i]);
    }

    curl_multi_cleanup(multi);
    return 0;
}

const char *construct_file_name(const char *key, char url[MAX_URL],
                                char outpath[MAX_URL]) {
    const char *fn = strrchr(key, '/');
    fn = fn ? fn + 1 : key;
    snprintf(url, MAX_URL, "https://smn-ar-wrf.s3.us-west-2.amazonaws.com/%s",
             key);
    snprintf(outpath, MAX_URL, RAW_DIR "%s", fn);

    return fn;
}

void *check_time(FILE **file, char outpath[MAX_URL]) {
    struct stat *st = malloc(sizeof(struct stat));
    if (stat(outpath, st) != 0) st->st_mtime = 0;
    *file = fopen(outpath, "wb");
    if (!*file) {
        perror("outpath");
        free(st);
        return NULL;
    }
    return st;
}

void multi_perform(CURLM *multi) {
    int still_running = 0;

    curl_multi_perform(multi, &still_running);

    while (still_running) {
        curl_multi_wait(multi, NULL, 0, 1000, NULL);
        curl_multi_perform(multi, &still_running);
    }
}

int check_response(CURL *handle, char outpath[MAX_URL], FILE *file) {
    if (!handle) return 1;
    long code = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &code);

    if (code == 200) {
        printf("Downloaded %s (HTTP %ld)\n", outpath, code);
    } else if (code == 304) {
        printf("%s not modified. Skipped.\n", outpath);
    } else {
        printf("Downloaded %s (HTTP %ld)\n", outpath, code);
    }
    fclose(file);
    return 0;
}

void set_master() {
    curl_easy_reset(master);

    curl_easy_setopt(master, CURLOPT_NOBODY, 0L);
    curl_easy_setopt(master, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(master, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(master, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    curl_easy_setopt(master, CURLOPT_SSL_SESSIONID_CACHE, 1L);
    curl_easy_setopt(master, CURLOPT_DNS_CACHE_TIMEOUT, 600L);
    curl_easy_setopt(master, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(master, CURLOPT_TCP_KEEPIDLE, 120L);
    curl_easy_setopt(master, CURLOPT_TCP_KEEPINTVL, 60L);
}

int main(void) {
    curl_global_init(CURL_GLOBAL_ALL);

    master = curl_easy_init();
    if (!master) {
        fprintf(stderr, "libcurl init failed\n");
        return 1;
    }

    set_master();

    struct tm run_tm;
    if (find_latest_run(&run_tm) != 0) {
        fprintf(stderr, "No valid run in the last 24H\n");
        curl_easy_cleanup(master);
        curl_global_cleanup();
        return 1;
    }

    set_master();

    time_t now = time(NULL);
    time_t run_time = timegm(&run_tm);
    double hours = difftime(now, run_time) / 3600;
    int offsets[NUM_FILES] = {(int)round(hours), (int)round(hours),
                              (int)round(hours / 24)};

    char keys[NUM_FILES][MAX_URL] = {{0}};
    generate_keys(&run_tm, offsets, keys);

    if (!keys[0][0] || !keys[1][0] || !keys[2][0]) {
        fprintf(stderr, "Missing one of 10M, 01H, 24H keys\n");
        curl_easy_cleanup(master);
        curl_global_cleanup();
        return 1;
    }

    download_parallel(keys);

    curl_easy_cleanup(master);
    curl_global_cleanup();
    return 0;
}
