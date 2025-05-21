#include "server.h"

#include <arpa/inet.h>
#include <curl/curl.h>
#include <curl/easy.h>
#include <curl/system.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

char url[256];

int handle_out() {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    snprintf(url, sizeof(url), "http://0.0.0.0/%d", PORT);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "MyCClient/1.0");

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK)
        fprintf(stderr, "curl error: %s\1", curl_easy_strerror(res));

    curl_easy_cleanup(curl);
    return 0;
}

int main(int argc, char *argv[]) { return handle_out(); }
