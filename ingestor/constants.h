/// @file
/// @brief Determinamos una serie de constantes para el ingestor.
#pragma once

/// Carpeta en la que se descarga la información de AWS.
#define RAW_DIR "/data/raw/netcdf/"

/// Carpeta en la que se descarga la información de open-meteo.
#define LIVE_DIR "/data/raw/live/"

/// Cantidad de archivos .nc subidos al servicio meteorológico nacional en AWS.
#define NUM_FILES 3

/// Largo máximo del URL de AWS.
#define MAX_URL 1024

/// Comando para ejecutar parse_and_store.py
#define PARSE_SCRIPT "python3 parse_and_store.py"

/// Dirección de la base de datos en el entorno de docker.
#define DB_PATH "/data/dev.db"
