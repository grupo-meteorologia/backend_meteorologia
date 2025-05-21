#include <curl/curl.h>
#include <curl/easy.h>
#include <curl/multi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

struct Memory {
    char *buf;
    size_t size;
};

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userp) {
    size_t total = size * nmemb;
    struct Memory *m = userp;
    char *newbuf = realloc(m->buf, m->size + total + 1);
    if (!newbuf) return 0;
    m->buf = newbuf;
    memcpy(m->buf + m->size, ptr, total);
    m->size += total;
    m->buf[m->size] = '\0';
    return total;
}

static time_t parse_time(const char *s) {
    struct tm tm = {0};
    sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
           &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    return timegm(&tm);
}

int download_parallel(CURL *master, const char *keys[3]) {
    CURLM *multi = curl_multi_init();
    CURL *handles[3];
    FILE *files[3];

    char urls[3][1024];
    char outpaths[3][1024];

    int i;
    int still_running;

    for (i = 0; i < 3; i++) {
        snprintf(urls[i], sizeof(urls[i]),
                 "https://smn-ar-wrf.s3.us-west-2.amazonaws.com/%s", keys[i]);

        const char *fn = strrchr(keys[i], '/');
        fn = fn ? fn + 1 : keys[i];
        snprintf(outpaths[i], sizeof(outpaths[i]), "./data/raw/%s", fn);

        files[i] = fopen(outpaths[i], "wb");
        if (!files[i]) {
            perror("fopen");
            goto cleanup_partial;
        }

        handles[i] = curl_easy_duphandle(master);
        curl_easy_setopt(handles[i], CURLOPT_URL, urls[i]);
        curl_easy_setopt(handles[i], CURLOPT_WRITEFUNCTION, NULL);
        curl_easy_setopt(handles[i], CURLOPT_WRITEDATA, files[i]);

        struct stat st;
        if (stat(outpaths[i], &st) == 0) {
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

    for (i = 0; i < 3; i++) {
        long code = 0;
        curl_easy_getinfo(handles[i], CURLINFO_RESPONSE_CODE, &code);

        if (code == 304) {
            printf("%s not modified. Skipped.\n", outpaths[i]);
        } else {
            printf("Downloaded %s (HTTP %ld)\n", outpaths[i], code);
        }

        curl_multi_remove_handle(multi, handles[i]);
        curl_easy_cleanup(handles[i]);
        fclose(files[i]);
    }
    return 0;

cleanup_partial:
    for (int j = 0; j < i; ++j) {
        curl_multi_remove_handle(multi, handles[i]);
        curl_easy_cleanup(handles[j]);
        fclose(files[j]);
    }
    curl_multi_cleanup(multi);
    curl_easy_cleanup(master);
    return 1;
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

    time_t now = time(NULL);

    struct tm utc;
    if (gmtime_r(&now, &utc) == NULL) {
        perror("gmtime_r");
        return 1;
    }

    int year = utc.tm_year + 1900;
    int month = utc.tm_mon + 1;
    int day = utc.tm_mday;
    int hour = (utc.tm_hour / 6) * 6;

    char prefix_url[512];
    snprintf(prefix_url, sizeof(prefix_url),
             "https://s3.us-west-2.amazonaws.com/"
             "smn-ar-wrf?list-type=2&prefix=DATA/WRF/DET/%04d/%02d/"
             "%02d/%02d/",
             year, month, day, hour);

    curl_easy_setopt(master, CURLOPT_URL, prefix_url);
    curl_easy_setopt(master, CURLOPT_NOBODY, 1L);
    curl_easy_perform(master);

    struct Memory mem = {.buf = NULL, .size = 0};

    for (int attempt = 0; attempt < 4; attempt++) {
        free(mem.buf);
        mem.buf = NULL;
        mem.size = 0;

        snprintf(prefix_url, sizeof(prefix_url),
                 "https://s3.us-west-2.amazonaws.com/"
                 "smn-ar-wrf?list-type=2&prefix=DATA/WRF/DET/%04d/%02d/"
                 "%02d/%02d/",
                 year, month, day, hour);

        curl_easy_setopt(master, CURLOPT_NOBODY, 0L);
        curl_easy_setopt(master, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(master, CURLOPT_WRITEDATA, &mem);
        curl_easy_setopt(master, CURLOPT_URL, prefix_url);
        if (curl_easy_perform(master) != CURLE_OK) {
            fprintf(stderr, "Error fetching list\n");
            return 1;
        }

        if (strstr(mem.buf, "<Contents>")) {
            printf("Found data in %02d UTC folder\n", hour);
            break;
        }

        hour -= 6;

        if (hour >= 0) continue;

        hour += 24;
        utc.tm_year = year - 1900;
        utc.tm_mon = month - 1;
        utc.tm_mday = day - 1;
        mktime(&utc);
        year = utc.tm_year + 1900;
        month = utc.tm_mon + 1;
        day = utc.tm_mday;
    }

    long code;
    curl_easy_getinfo(master, CURLINFO_RESPONSE_CODE, &code);
    printf("LIST returned HTTP %ld, %zu bytes of XML:\n%s\n", code, mem.size,
           mem.buf);

    time_t latest = 0;
    char latest_key[512] = {0};

    time_t t10m = 0, t1h = 0, t24h = 0;
    char *p = mem.buf, k10m[512] = {0}, k1h[512] = {0}, k24h[512] = {0};
    while ((p = strstr(p, "<Contents>"))) {
        char *k_start = strstr(p, "<Key>");
        char *k_end = strstr(k_start, "</Key>");
        char *l_start = strstr(k_end, "<LastModified>");
        char *l_end = strstr(l_start, "</LastModified>");

        if (!k_start || !k_end || !l_start || !l_end) break;

        k_start += strlen("<Key>");
        l_start += strlen("<LastModified>");

        char k_str[512] = {0};
        char ts[32] = {0};
        size_t k_len = k_end - k_start;
        size_t ts_len = l_end - l_start;

        if (k_len >= sizeof(k_str)) k_len = sizeof(k_str) - 1;
        memcpy(k_str, k_start, k_len);
        k_str[k_len] = '\0';

        printf("key_str: %s\n", k_str);

        if (k_len < 4 || strcmp(k_str + k_len - 3, ".nc") != 0) {
            p = l_end;
            continue;
        }

        if (ts_len >= sizeof(ts)) ts_len = sizeof(ts) - 1;
        memcpy(ts, l_start, ts_len);
        ts[ts_len] = '\0';

        time_t t = parse_time(ts);

        if (strstr(k_str, "_10M_") && t > t10m) {
            t10m = t;
            strncpy(k10m, k_str, sizeof(k10m) - 1);
        } else if (strstr(k_str, "_01H_") && t > t1h) {
            t1h = t;
            strncpy(k1h, k_str, sizeof(k1h) - 1);
        } else if (strstr(k_str, "_24H_") && t > t24h) {
            t24h = t;
            strncpy(k24h, k_str, sizeof(k24h) - 1);
        }

        p = l_end;
    }

    free(mem.buf);

    const char *keys[3] = {k10m, k1h, k24h};

    if (!keys[0][0] || !keys[1][0] || !keys[2][0]) {
        fprintf(stderr, "Missing one of 10M, 01H, 24H keys\n");
        curl_easy_cleanup(master);
        return 1;
    }

    printf("Newest .nc are:\n\tk10m => %s\n\tk1h => %s\n\tk24h => %s\n",
           keys[0], keys[1], keys[2]);

    download_parallel(master, keys);

    curl_easy_cleanup(master);
    return 0;
}
