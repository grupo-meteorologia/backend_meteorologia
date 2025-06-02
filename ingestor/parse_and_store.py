#!/usr/bin/env python3
import argparse
import sqlite3
import json
import sys
import os
from netCDF4 import Dataset
import numpy as np

DB_PATH = "/data/dev.db"
RAW_DIR = "/data/raw/netcdf/"
LIVE_DIR = "/data/raw/live/"

VAR_SPECS = {
    "T2":         {"source": "01H", "key": "temperature",     "tolerance": 300},
    "HR2":        {"source": "01H", "key": "humidity",        "tolerance": 1800},
    "magViento10":{"source": "01H", "key": "wind_speed",      "tolerance": 1800},
    "dirViento10":{"source": "01H", "key": "wind_direction",  "tolerance": 1800},
    "PSFC":       {"source": "01H", "key": "pressure",        "tolerance": 3600},
    "Tmax":       {"source": "24H", "key": "temp_max",        "tolerance": 43200},
    "Tmin":       {"source": "24H", "key": "temp_min",        "tolerance": 43200},
}

LIVE_KEYS = {
    "temperature", "humidity"
}

def parse_args():
    p = argparse.ArgumentParser(
        description="Subset a NetCDF file by lon/lat box and export JSON without xarray"
    )
    p.add_argument("timestamp", help="timestamp, YYYY-MM-DD hh:mm:ss")
    p.add_argument("lon", type=float, help="longitude")
    p.add_argument("lat", type=float, help="latitude")
    return p.parse_args()

def point_index(array, value):
    return int(np.argmin(np.abs(array-value)))

def weather_exists(db, var, lat, lon, ts, tol):
    q = """
        SELECT 1 FROM weather
        WHERE variable = ?
        AND ABS(lat - ?) < 0.01
        AND ABS(lon - ?) < 0.01
        AND ABS(strftime('%s', timestamp) - strftime('%s', ?)) < ?
        LIMIT 1
    """
    return db.execute(q, (var, lat, lon, ts, tol)).fetchone() is not None

def insert_weather(db, var, val, lat, lon, ts):
    db.execute(
        "INSERT INTO weather (variable, value, lat, lon, timestamp) VALUES (?, ?, ?, ?, ?)",
        (var, val, lat, lon, ts)
    )


def load_live_data(key, lat, lon):
    for name in os.listdir(LIVE_DIR):
        if not name.endswith(".json"):
            continue
        path = os.path.join(LIVE_DIR, name)
        try:
            with open(path) as f:
                data = json.load(f)
            variables = data.get("variables", {})
            if key not in variables:
                continue

            lons = np.array(variables.get("lon", {}).get("data", []))
            lats = np.array(variables.get("lat", {}).get("data", []))

            if lons.size == 0 or lats.size == 0:
                continue

            
            ix = point_index(lons, lon)
            iy = point_index(lats, lat)

            vals = np.array(variables[key]["data"]).reshape(variables[key]["shape"])
            return float(vals[iy][ix])

        except Exception as e:
            continue

    return None


def process_variable(varname, spec, ts, lat, lon, db):
    key = spec["key"]
    tol = spec["tolerance"]

    if weather_exists(db, key, lat, lon, ts, tol):
        return

    if key in LIVE_KEYS:
        val = load_live_data(key, lat, lon)
        if val is not None:
            insert_weather(db, key, val, lat, lon, ts)
            print(f"(LIVE) Inserted {key} = {val:.2f} at ({lat}, {lon})")
            return

    source = spec["source"]
    ncfiles = [f for f in os.listdir(RAW_DIR) if source in f]

    if not ncfiles:
        print("No {source} files found.")
        return

    path = os.path.join(RAW_DIR, sorted(ncfiles)[-1])
    try:
        ds = Dataset(path, "r")
        lons = ds.variables["lon"][:]
        lats = ds.variables["lat"][:]
        data = ds.variables[varname][:]
        ds.close()
    except Exception as e:
        print(f"Error loading {path}: {e}", file=sys.stderr)
        return
    
    ix = point_index(lons, lon)
    iy = point_index(lats, lat)

    try:
        val = float(data[0][iy][ix])
    except Exception:
        try:
            val = float(data[iy][ix])
        except Exception:
            print(f"Could not read value for {varname}")
            return

    insert_weather(db, spec["key"], val, lat, lon, ts)
    print(f"Inserted {spec['key']} = {val:.2f} at ({lat}, {lon})")


def main():
    args = parse_args()
    ts = args.timestamp
    lat, lon = args.lat, args.lon

    try:
        db = sqlite3.connect(DB_PATH)
    except Exception as e:
        print(f"DB error: {e}")
        sys.exit(1)

    for varname, spec in VAR_SPECS.items():
        process_variable(varname, spec, ts, lat, lon, db)

    db.commit()
    db.close()

if __name__ == '__main__':
    main()

