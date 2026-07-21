#!/usr/bin/env python3
"""
build_targets.py - Clean and unify the target aimpoint lists into data/targets.json.

Sources:
  prepatory_CSVs/large_list.csv  (1087 aimpoints; State,Target,Category,Lat,Lng,Yield,Type)
  prepatory_CSVs/MASTER.csv      (curated subset with embedded-terrain IDs)

Cleanups performed:
  - strip UTF-8 BOM
  - fix the "Militaryj" category typo -> "Military"
  - parse "500kt" -> 500 (int kt); "1.2mt" -> 1200 supported too
  - parse "Air Burst"/"Surface Burst" -> "air"/"surface"
  - assign stable integer IDs (source order)
  - flag duplicate (name,state) aimpoints (multiple DGZ per city) with an index

Output: data/targets.json  (array of target objects), plus a summary to stdout.
This is a pure data step; it does not touch engine code.
"""
import csv, json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LARGE = os.path.join(ROOT, "prepatory_CSVs", "large_list.csv")
OUT   = os.path.join(ROOT, "data", "targets.json")

def parse_yield(s):
    s = s.strip().lower().replace(" ", "")
    m = re.match(r"([0-9]*\.?[0-9]+)(kt|mt)?", s)
    if not m:
        return None
    val = float(m.group(1))
    unit = m.group(2) or "kt"
    return val * (1000.0 if unit == "mt" else 1.0)

def parse_burst(s):
    s = s.strip().lower()
    if "surface" in s or "ground" in s:
        return "surface"
    return "air"

def clean_category(c):
    c = c.strip()
    return "Military" if c.lower() == "militaryj" else c

def main():
    if not os.path.exists(LARGE):
        print(f"ERROR: {LARGE} not found", file=sys.stderr)
        return 1

    with open(LARGE, "r", encoding="utf-8-sig", newline="") as f:
        reader = csv.reader(f)
        header = next(reader)
        rows = [r for r in reader if any(cell.strip() for cell in r)]

    targets = []
    seen = {}
    cat_counts = {}
    fixed_typos = 0
    for i, r in enumerate(rows):
        if len(r) < 7:
            print(f"  WARN row {i+2}: only {len(r)} fields, skipping: {r}", file=sys.stderr)
            continue
        state, name, category, lat, lon, yld, typ = r[0], r[1], r[2], r[3], r[4], r[5], r[6]
        cat = clean_category(category)
        if cat != category.strip():
            fixed_typos += 1
        key = (name.strip().lower(), state.strip().lower())
        seen[key] = seen.get(key, 0) + 1
        y = parse_yield(yld)
        obj = {
            "id": i + 1,
            "name": name.strip(),
            "state": state.strip(),
            "category": cat,
            "lat": float(lat),
            "lon": float(lon),
            "yield_kt": y,
            "burst": parse_burst(typ),
            "aimpoint_index": seen[key],
        }
        targets.append(obj)
        cat_counts[cat] = cat_counts.get(cat, 0) + 1

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump({"count": len(targets), "targets": targets}, f, indent=1)

    n_multi = sum(1 for k, v in seen.items() if v > 1)
    print(f"Wrote {OUT}")
    print(f"  targets: {len(targets)}")
    print(f"  categories: {cat_counts}")
    print(f"  fixed 'Militaryj' typos: {fixed_typos}")
    print(f"  cities with multiple aimpoints: {n_multi}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
