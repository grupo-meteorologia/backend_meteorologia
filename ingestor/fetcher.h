#pragma once

#include <curl/curl.h>
#include <curl/easy.h>
#include <curl/multi.h>
#include <curl/system.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "constants.h"

static CURL *master = NULL;

int find_latest_run(struct tm *out);

void generate_keys(const struct tm *run, int offset[NUM_FILES],
                   char keys[NUM_FILES][MAX_URL]);

int fetch_current_conditions(double lat, double lon, char *out_path,
                             size_t out_path_size);

int download_parallel(const char keys[NUM_FILES][MAX_URL]);

void multi_perform(CURLM *multi);

int check_response(CURL *handle, char outpath[MAX_URL], FILE *file);

const char *construct_file_name(const char *key, char url[MAX_URL],
                                char outpath[MAX_URL]);

void *check_time(FILE **file, char outpath[MAX_URL]);

bool run_already_downloaded(struct tm *run_tm);

void set_master();
