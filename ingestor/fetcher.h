/// @file
/// @brief Ubicamos y descargamos archivos de AWS y open-meteo.
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

/// \ Puntero a la instancia global de CURL, definida en fetcher.c
extern CURL *master;

/// \ Función para devolver los últimos 3 archivos .nc del servicio
/// meteorológico
/// \ nacional guardados en AWS. Devuelve 1 si es incapaz de ejecutar las
/// \ instrucciones o si no hay una run válida en las últimas 24 horas.
int find_latest_run(struct tm *out);

/// \ Función que, utilizando find_latest_run, produce llaves válidas para AWS.
void generate_keys(const struct tm *run, int offset[NUM_FILES],
                   char keys[NUM_FILES][MAX_URL]);

/// \ Función que busca la información desde open-meteo.
int fetch_current_conditions(double lat, double lon, char *out_path,
                             size_t out_path_size);

/// \ Función que, dadas las llaves de AWS, abre una cierta cantidad de hilos y
/// \ descarga los archivos en paralelo.
int download_parallel(const char keys[NUM_FILES][MAX_URL]);

/// \ Función de apoyo para que la thread desde la que se lanza
/// download_parallel
/// \ espere a que esta se complete. Es llamada desde dentro de
/// download_parallel
/// \ unicamente.
void multi_perform(CURLM *multi);

/// \ NO USEN ESTO. Tiene un uso muy concreto en download_parallel.
int check_response(CURL *handle, char outpath[MAX_URL], FILE *file);

/// \ Pone el archivo descargado en /data/netcdf/
const char *construct_file_name(const char *key, char url[MAX_URL],
                                char outpath[MAX_URL]);

/// \ Bien simple de entender. Observa un url y determina compara el tiempo con
/// \ un
/// \ archivo.
void *check_time(FILE **file, char outpath[MAX_URL]);

/// \ Determina si un archivo .nc fue descargado anteriormente o no.
bool run_already_downloaded(struct tm *run_tm);

int cpy_into_permanent(const char outpaths[NUM_FILES][MAX_URL]);

/// \ Configura variables de master de vuelta al predeterminado.
void set_master();
