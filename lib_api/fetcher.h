#pragma once

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

#define WIDTH 50
#define RAW_DIR "data/raw/"
#define NUM_FILES 3
#define MAX_URL 1024

static int find_latest_run(struct tm *out);

static void generate_keys(const struct tm *run, int offset[NUM_FILES],
                          char keys[NUM_FILES][MAX_URL]);

int download_parallel(const char keys[NUM_FILES][MAX_URL]);

void multi_perform(CURLM *multi);

int check_response(CURL *handle, char outpath[MAX_URL], FILE *file);

const char *construct_file_name(const char *key, char url[MAX_URL],
                                char outpath[MAX_URL]);

void *check_time(FILE **file, char outpath[MAX_URL]);

void set_master();
