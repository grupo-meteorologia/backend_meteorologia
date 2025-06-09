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

SPEC_FILE = "/data/var_specs.json"
try:
    with open(SPEC_FILE, "r") as sf:
        VAR_SPECS = json.load(sf)
except Exception as e:
    print(f"ERROR loading {SPEC_FILE}: {e}", file=sys.stderr)
    sys.exit(1)

LIVE_KEYS = {
    "temperature", "humidity"
}

def parse_args():
    p = argparse.ArgumentParser(
        description="Subset a NetCDF file by lon/lat box and export JSON without xarray"
    )
    p.add_argument("timestamp", help="timestamp, YYYY-MM-DD hh:mm:ss")
    p.add_argument("lat", type=float, help="latitude")
    p.add_argument("lon", type=float, help="longitude")
    p.add_argument("--ready", action="store_true", help="if set, live data (LIVE_KEYS) can be used")
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

    try:
        db.commit()
    except sqlite3.OperationalError as e:
        # one last retry
        print(f"Commit failed, retrying: {e}", file=sys.stderr)
        db.commit()


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


def process_variable(varname, spec, ts, lat, lon, db, ready):
    key = spec["key"]
    tol = spec["tolerance"]

    if weather_exists(db, key, lat, lon, ts, tol):
        return

    if ready and key in LIVE_KEYS:
        val = load_live_data(key, lat, lon)
        if val is not None:
            insert_weather(db, key, val, lat, lon, ts)
            print(f"(LIVE) Inserted {key} = {val:.2f} at ({lat}, {lon})")
            return

    source = spec["source"]
    ncfiles = [f for f in os.listdir(RAW_DIR) if source in f]
    if not ncfiles:
        print(f"no {source} files found.")
        return

    path = os.path.join(RAW_DIR, sorted(ncfiles)[-1])
    try:
        ds = Dataset(path, "r")
        lats = ds.variables["lat"][:]
        lons = ds.variables["lon"][:]
        arr = ds.variables[varname][:]
        ds.close()
    except Exception as e:
        print(f"Error loading {path}: {e}", file=sys.stderr)
        return

    dist2 = (lats - lat)**2 + (lons - lon)**2
    flat_idx = np.argmin(dist2)
    
    iy, ix = np.unravel_index(flat_idx, dist2.shape)

    dims = arr.shape
    if len(dims) < 2:
        print(f"Could not reaad values for {varname} bad dims: {dims}")

    prefix = (0,) * (arr.ndim-2)

    try:
        val = float(arr[prefix + (iy, ix)])
    except Exception as e:
        print(f"Could not read value for {varname}: {e}")
        return

    insert_weather(db, key, val, lat, lon, ts)
    print(f"Inserted {key} = {val:.2f} at ({lat}, {lon})")


def main():
    args = parse_args()
    ts = args.timestamp
    lat, lon = args.lat, args.lon
    use_live = args.ready

    try:
        db = sqlite3.connect(DB_PATH, timeout=60)
        db.execute("PRAGMA journal_mode = WAL;")
        db.execute("PRAGMA busy_timeout = 60000;")
    except Exception as e:
        print(f"DB error: {e}", file=sys.stderr)
        sys.exit(1)

    for varname, spec in VAR_SPECS.items():
        process_variable(varname, spec, ts, lat, lon, db, use_live)

    db.close()


if __name__ == '__main__':
    main()

