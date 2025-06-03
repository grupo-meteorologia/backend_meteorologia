#include <pthread.h>
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "constants.h"
#include "fetcher.h"
#include "parser.h"

int current_data_sql() {
    fprintf(stderr, "[DEBUG] current_data_sql\n");
    sqlite3* db;
    if (sqlite3_open(DB_PATH, &db) != SQLITE_OK) return 1;

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v3(
        db, "SELECT DISTINCT lat, lon FROM missing_requests;", -1, 0, &stmt,
        NULL);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "ERROR preparando SELECT en current_data_sql: %s\n",
                sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        double lat = sqlite3_column_double(stmt, 0);
        double lon = sqlite3_column_double(stmt, 1);

        char path[256];
        if (fetch_current_conditions(lat, lon, path, sizeof(path)) != 0) {
            fprintf(stderr, "Error al obtener clima actual para %.2f, %.2f\n",
                    lat, lon);
            sqlite3_finalize(stmt);
            sqlite3_close(db);
            return 1;
        }

        sqlite3_stmt* update = NULL;
        const char* query =
            "UPDATE missing_requests SET ready = 1 WHERE lat = ? AND lon = ?;";
        if (sqlite3_prepare_v3(db, query, -1, 0, &update, NULL) == SQLITE_OK) {
            sqlite3_bind_double(update, 1, lat);
            sqlite3_bind_double(update, 2, lon);
            sqlite3_step(update);
            sqlite3_finalize(update);
        }
        fprintf(stderr, "[DEBUG] Fetched current weather conditions.\n");
    }

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "ERROR al recorrer missing requests: %s\n",
                sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return 1;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return 0;
}

static void sqlite_update_hook(void* p_user_data, int op, const char* db_name,
                               const char* table_name, sqlite3_int64 id) {
    fprintf(stderr, "[DEBUG] sqlite_update_hook\n");
    if (op != SQLITE_UPDATE) return;
    if (strcmp(table_name, "missing_requests") != 0) return;

    sqlite3* db = (sqlite3*)p_user_data;

    sqlite3_stmt* stmt = NULL;
    const char* check_query =
        "SELECT lat, lon, timestamp, ready FROM missing_requests WHERE id = "
        "?;";

    if (sqlite3_prepare_v3(db, check_query, -1, 0, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "[db_hook] ERROR preparando SELECT: %s\n",
                sqlite3_errmsg(db));
        return;
    }
    sqlite3_bind_int64(stmt, 1, id);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return;
    }
    int ready = sqlite3_column_int(stmt, 3);
    if (ready != 1) {
        sqlite3_finalize(stmt);
        return;
    }

    double lat = sqlite3_column_double(stmt, 0);
    double lon = sqlite3_column_double(stmt, 1);
    const unsigned char* uts = sqlite3_column_text(stmt, 2);

    if (uts && *uts) {
        char timestamp[32] = {0};
        strncpy(timestamp, (const char*)uts, sizeof(timestamp) - 1);
        int result = call_parse_and_store(timestamp, lat, lon);
        if (result != 0) {
            fprintf(stderr,
                    "[db_hook] ERROR al parsear y almacenar para %.2f, %.2f\n",
                    lat, lon);

            sqlite3_finalize(stmt);
            return;
        }
    }

    sqlite3_finalize(stmt);

    sqlite3_stmt* del = NULL;
    const char* dq = "DELETE FROM missing_requests WHERE id = ?;";
    if (sqlite3_prepare_v3(db, dq, -1, 0, &del, NULL) != SQLITE_OK) {
        fprintf(stderr, "[db_hook] Error preparando DELETE: %s\n",
                sqlite3_errmsg(db));
        return;
    }
    sqlite3_bind_int64(del, 1, id);
    if (sqlite3_step(del) != SQLITE_DONE) {
        fprintf(stderr, "[db_hook] Error ejecutando DELETE: %s\n",
                sqlite3_errmsg(db));
    }
    sqlite3_finalize(del);
}

void* fetcher_thread(void* _) {
    fprintf(stderr, "[DEBUG] fetcher_thread\n");
    curl_global_init(CURL_GLOBAL_ALL);

    master = curl_easy_init();
    if (!master) {
        fprintf(stderr, "[ERROR] libcurl init failed\n");
        return NULL;
    }
    fprintf(stderr, "[DEBUG] Master global is not NULL\n");

    set_master();

    while (1) {
        if (current_data_sql() != 0) {
            fprintf(stderr,
                    "current_data_sql devolvió un error, se intenta de nuevo "
                    "en 60s\n");
        }

        struct tm run_tm;
        int rc_1 = find_latest_run(&run_tm);
        int rc_2 = run_already_downloaded(&run_tm);
        if (rc_1 != 0 || rc_2) {
            fprintf(stderr,
                    "[DEBUG] Fetcher sleeping\n\tfind_latest_run(&run_tm) = "
                    "%d\n\trun_already_downloaded = %d\n",
                    rc_1, rc_2);
            sleep(60);
            continue;
        }
        int offsets[NUM_FILES] = {0};
        char keys[NUM_FILES][MAX_URL] = {{0}};
        generate_keys(&run_tm, offsets, keys);
        download_parallel(keys);

        sleep(60);
    }

    // NOT REACHED
    // curl_easy_cleanup(master);
    // curl_global_cleanup();
    return NULL;
}

void* parser_thread(void* _) {
    fprintf(stderr, "[DEBUG] parser_thread\n");
    sqlite3* db;
    if (sqlite3_open_v2(DB_PATH, &db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX,
                        NULL) != SQLITE_OK) {
        fprintf(stderr, "[parser_thread] ERROR abriendo DB: %s\n",
                sqlite3_errmsg(db));
        return NULL;
    }

    while (1) {
        fprintf(stderr, "[DEBUG] Hell loop from parser_thread.\n");
        sqlite3_stmt* stmt = NULL;
        const char* query =
            "SELECT lat, lon, timestamp, id FROM missing_requests WHERE "
            "ready = 1;";
        if (sqlite3_prepare_v3(db, query, -1, 0, &stmt, NULL) != SQLITE_OK) {
            fprintf(stderr, "[parser_thread] Error preparando SELECT: %s\n",
                    sqlite3_errmsg(db));
            sleep(600);
            continue;
        }
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            double lat = sqlite3_column_double(stmt, 0);
            double lon = sqlite3_column_double(stmt, 1);
            const unsigned char* uts = sqlite3_column_text(stmt, 2);
            sqlite3_int64 id = sqlite3_column_int64(stmt, 3);

            if (uts) {
                char timestamp[32];
                strncpy(timestamp, (const char*)uts, sizeof(timestamp) - 1);
                timestamp[sizeof(timestamp) - 1] = '\0';
                call_parse_and_store(timestamp, lat, lon);
            }

            sqlite3_stmt* del = NULL;
            const char* query = "DELETE FROM missing_requests WHERE id = ?;";
            if (sqlite3_prepare_v3(db, query, -1, 0, &del, NULL) == SQLITE_OK) {
                sqlite3_bind_int64(del, 1, id);
                sqlite3_step(del);
                sqlite3_finalize(del);
            }
        }
        sqlite3_finalize(stmt);
        sleep(600);
    }
    return NULL;
}

void* db_thread(void* _) {
    fprintf(stderr, "[DEBUG] db_thread\n");
    sqlite3* db = NULL;
    if (sqlite3_open_v2(DB_PATH, &db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX,
                        NULL) != SQLITE_OK) {
        fprintf(stderr, "[db_thread] Error abriendo DB: %s\n",
                sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        return NULL;
    }

    sqlite3_update_hook(db, sqlite_update_hook, db);

    while (1) {
        sleep(600);
    }
    return NULL;
}

int main(void) {
    fprintf(stderr, "[DEBUG] === entering main() ===\n");
    fflush(stderr);
    pthread_t t_fetcher, t_parser, t_db;

    fprintf(stderr, "[DEBUG] Alo\n");

    pthread_create(&t_fetcher, NULL, fetcher_thread, NULL);
    pthread_create(&t_parser, NULL, parser_thread, NULL);
    pthread_create(&t_db, NULL, db_thread, NULL);

    fprintf(stderr, "[DEBUG] Threads opened\n");

    pthread_join(t_fetcher, NULL);
    pthread_join(t_parser, NULL);
    pthread_join(t_db, NULL);

    // struct tm run_tm;
    // if (find_latest_run(&run_tm) != 0) {
    //     fprintf(stderr, "No valid run in the last 24H\n");
    //     curl_easy_cleanup(master);
    //     curl_global_cleanup();
    //     return 1;
    // }
    //
    // set_master();
    //
    // time_t now = time(NULL);
    // time_t run_time = timegm(&run_tm);
    // double hours = difftime(now, run_time) / 3600;
    // int offsets[NUM_FILES] = {(int)round(hours), (int)round(hours),
    //                           (int)round(hours / 24)};
    //
    // char keys[NUM_FILES][MAX_URL] = {{0}};
    // generate_keys(&run_tm, offsets, keys);
    //
    // if (!keys[0][0] || !keys[1][0] || !keys[2][0]) {
    //     fprintf(stderr, "Missing one of 10M, 01H, 24H keys\n");
    //     curl_easy_cleanup(master);
    //     curl_global_cleanup();
    //     return 1;
    // }
    //
    // download_parallel(keys);
    //
    // curl_easy_cleanup(master);
    // curl_global_cleanup();
    //
    // sqlite3* db;
    // if (sqlite3_open(DB_PATH, &db) != SQLITE_OK) {
    //     fprintf(stderr, "Error opening DB: %s\n", sqlite3_errmsg(db));
    //     return 1;
    // }
    //
    // sqlite3_stmt* stmt;
    // const char* sql = "SELECT lat, lon FROM missing_requests;";
    // if (sqlite3_prepare_v3(db, sql, -1, 0, &stmt, NULL) != SQLITE_OK) {
    //     fprintf(stderr, "Error preparing SELECT: %s\n", sqlite3_errmsg(db));
    //     sqlite3_close(db);
    //     return 1;
    // }
    //
    // while (sqlite3_step(stmt) == SQLITE_ROW) {
    //     double lat = sqlite3_column_double(stmt, 0);
    //     double lon = sqlite3_column_double(stmt, 1);
    //
    //     printf("Procesando lat=%.2f, lon=%.2f\n", lat, lon);
    // }
    //
    // sqlite3_finalize(stmt);
    // sqlite3_close(db);

    return 0;
}
