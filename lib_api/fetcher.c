#include <curl/curl.h>
#include <curl/easy.h>
#include <curl/multi.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define RAW_DIR "data/raw/"
#define NUM_FILES 3
#define MAX_URL 1024

static int find_latest_run(CURL *master, struct tm *out) {
    if (!master) return 1;

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
            curl_easy_cleanup(master);
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

int download_parallel(CURL *master, const char keys[NUM_FILES][MAX_URL]) {
    CURLM *multi = curl_multi_init();
    if (!multi) return 1;

    CURL *handles[NUM_FILES] = {0};
    FILE *files[NUM_FILES] = {0};

    int still_running = 0;
    char url[MAX_URL] = {0}, outpath[NUM_FILES][MAX_URL] = {{0}};

    for (int i = 0; i < 3; i++) {
        const char *fn = strrchr(keys[i], '/');
        fn = fn ? fn + 1 : keys[i];
        snprintf(url, sizeof(url),
                 "https://smn-ar-wrf.s3.us-west-2.amazonaws.com/%s", keys[i]);
        snprintf(outpath[i], sizeof(outpath), RAW_DIR "%s", fn);

        struct stat st;
        time_t mtime = 0;
        if (stat(outpath[i], &st) == 0) mtime = st.st_mtime;
        files[i] = fopen(outpath[i], "wb");
        if (!files[i]) {
            perror("outpath");
            continue;
        }

        handles[i] = curl_easy_duphandle(master);
        curl_easy_setopt(handles[i], CURLOPT_URL, url);
        curl_easy_setopt(handles[i], CURLOPT_WRITEFUNCTION, NULL);
        curl_easy_setopt(handles[i], CURLOPT_WRITEDATA, files[i]);

        if (mtime != 0) {
            curl_easy_setopt(handles[i], CURLOPT_TIMECONDITION,
                             CURL_TIMECOND_IFMODSINCE);
            curl_easy_setopt(handles[i], CURLOPT_TIMEVALUE, (long)st.st_mtime);
        }

        curl_multi_add_handle(multi, handles[i]);
    }

    curl_multi_perform(multi, &still_running);
    while (still_running) {
        int numfds;
        curl_multi_wait(multi, NULL, 0, 1000, &numfds);
        curl_multi_perform(multi, &still_running);
    }

    for (int i = 0; i < NUM_FILES; i++) {
        if (!handles[i]) continue;

        long code = 0;
        curl_easy_getinfo(handles[i], CURLINFO_RESPONSE_CODE, &code);

        if (code == 200) {
            printf("Downloaded %s (HTTP %ld)\n", outpath[i], code);
        } else if (code == 304) {
            printf("%s not modified. Skipped.\n", outpath[i]);
        } else {
            printf("Downloaded %s (HTTP %ld)\n", outpath[i], code);
        }

        curl_multi_remove_handle(multi, handles[i]);
        curl_easy_cleanup(handles[i]);
        fclose(files[i]);
    }

    curl_multi_cleanup(multi);
    return 0;
}

int main(void) {
    CURL *master = curl_easy_init();
    if (!master) {
        fprintf(stderr, "libcurl init failed\n");
        return 1;
    }

    curl_easy_setopt(master, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(master, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    curl_easy_setopt(master, CURLOPT_SSL_SESSIONID_CACHE, 1L);
    curl_easy_setopt(master, CURLOPT_DNS_CACHE_TIMEOUT, 600L);
    curl_easy_setopt(master, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(master, CURLOPT_TCP_KEEPIDLE, 120L);
    curl_easy_setopt(master, CURLOPT_TCP_KEEPINTVL, 60L);

    struct tm run_tm;
    if (find_latest_run(master, &run_tm) != 0) {
        fprintf(stderr, "No valid run in the last 24H\n");
        curl_easy_cleanup(master);
        return 1;
    }

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
        return 1;
    }

    download_parallel(master, keys);

    curl_easy_cleanup(master);
    return 0;
}
