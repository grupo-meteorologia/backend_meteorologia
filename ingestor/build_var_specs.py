import os, json, re, sys
from netCDF4 import Dataset

RAW_DIR = "/data/raw/netcdf/"
OUT_FILE = "/data/var_specs.json"

SOURCE_TOL = {
    "10M": 600,
    "01H": 3600,
    "24H": 86400,
}
TAGS = ( "10M", "01H", "24H" )

def extract_source(fname):
    return next((tag for tag in TAGS if tag in fname), None)

def is_grid(var):
    dims = var.dimensions

    if dims == ("lat", "lon") or dims == ("y", "x"):
        return True
    if len(dims)==3 and dims[0]=="time" and (dims[1:],) in [(("lat","lon"),), (("y","x"),)]:
        return True

    return False

def build_specs():
    specs = {}
    for fname in sorted(os.listdir(RAW_DIR)):
        if not fname.endswith(".nc"):
            continue

        src = extract_source(fname)
        tol = SOURCE_TOL[src]
        path = os.path.join(RAW_DIR, fname)

        try:
            ds = Dataset(path, "r")
            for varname, var in ds.variables.items():
                if varname in ("lat", "lon"):
                    continue
                if not is_grid(var):
                    continue
                if varname not in specs:
                    specs[varname] = {
                            "source": src,
                            "key": varname.lower(),
                            "tolerance": tol
                        }
            ds.close()
    
        except Exception as e:
            print(f"⚠️ Skipping {path}: {e}", file=sys.stderr)

    with open(OUT_FILE, "w") as fp:
        json.dump(specs, fp, indent = 2)

    print(f"Wrote {len(specs)} entries to {OUT_FILE}")

if __name__ == "__main__":
    build_specs()
