#!/usr/bin/env python3
"""
Subset a NetCDF file by lon/lat bounding box using netCDF4+numpy and output JSON.
No xarray dependency.
"""
import argparse
import json
import sys
from netCDF4 import Dataset
import numpy as np

def parse_args():
    p = argparse.ArgumentParser(
        description="Subset a NetCDF file by lon/lat box and export JSON without xarray"
    )
    p.add_argument("infile", help="Input NetCDF file path")
    p.add_argument("outfile", help="Output JSON file path")
    p.add_argument("--lon-min", type=float, required=True, help="Minimum longitude")
    p.add_argument("--lon-max", type=float, required=True, help="Maximum longitude")
    p.add_argument("--lat-min", type=float, required=True, help="Minimum latitude")
    p.add_argument("--lat-max", type=float, required=True, help="Maximum latitude")
    return p.parse_args()


def safe_json_value(val):
    """
    Convert numpy types to native Python types for JSON serialization.
    """
    if isinstance(val, np.generic):
        return val.item()
    if isinstance(val, np.ndarray):
        return val.tolist()
    # bytes -> string
    if isinstance(val, (bytes, bytearray)):
        try:
            return val.decode('utf-8')
        except Exception:
            return val.decode('latin-1', 'ignore')
    return val


def main():
    args = parse_args()

    try:
        ds = Dataset(args.infile, mode='r')
    except Exception as e:
        print(f"Error opening {args.infile}: {e}", file=sys.stderr)
        sys.exit(1)

    # Read coordinate arrays
    try:
        lon = ds.variables['lon'][:]
        lat = ds.variables['lat'][:]
    except KeyError:
        print("Coordinate variables 'lon' and/or 'lat' not found", file=sys.stderr)
        sys.exit(1)

    # Determine index ranges
    lon_idx = np.where((lon >= args.lon_min) & (lon <= args.lon_max))[0]
    lat_idx = np.where((lat >= args.lat_min) & (lat <= args.lat_max))[0]
    if lon_idx.size == 0 or lat_idx.size == 0:
        print("No data in specified lon/lat range", file=sys.stderr)
        sys.exit(1)
    lon_slice = slice(lon_idx.min(), lon_idx.max()+1)
    lat_slice = slice(lat_idx.min(), lat_idx.max()+1)

    # Build output
    out = {
        'dimensions': {dim: len(ds.dimensions[dim]) for dim in ds.dimensions},
        'global_attributes': {},
        'variables': {}
    }

    # Serialize global attributes safely
    for attr in ds.ncattrs():
        raw = ds.getncattr(attr)
        out['global_attributes'][attr] = safe_json_value(raw)

    # Process variables
    for name, var in ds.variables.items():
        data = var[:]
        dims = var.dimensions
        # Build slicing tuple
        slices = []
        for d in dims:
            if d == 'lon':
                slices.append(lon_slice)
            elif d == 'lat':
                slices.append(lat_slice)
            else:
                slices.append(slice(None))
        try:
            sub = data[tuple(slices)]
        except Exception:
            continue

        arr = np.array(sub)
        flat = arr.flatten().tolist()
        out['variables'][name] = {
            'dimensions': list(dims),
            'shape': list(arr.shape),
            'data': flat
        }

    # Write JSON
    try:
        with open(args.outfile, 'w') as f:
            json.dump(out, f, indent=2)
    except Exception as e:
        print(f"Error writing JSON {args.outfile}: {e}", file=sys.stderr)
        sys.exit(1)

    ds.close()

if __name__ == '__main__':
    main()

