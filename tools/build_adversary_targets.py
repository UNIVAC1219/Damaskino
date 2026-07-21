#!/usr/bin/env python3
"""
build_adversary_targets.py - Build data/targets_adversary.json (same schema as
data/targets.json) from two sourced inputs, with per-row provenance so "real"
vs "modeled" is transparent.

INPUTS
  1. prepatory_CSVs/usa_targets_1956.csv
     The canonical Wellerstein / Future of Life digitization of the 1956 SAC
     "Atomic Weapons Requirements Study" AIR POWER (airfields) list.
     Columns: city,country,lat,lng,alliance  (USSR / WarsawPact / ChinaDPRK).
     Provenance & modeling choices (all flagged per row):
       - Every point is an "Air Power" (BRAVO) airfield DGZ -> category is set
         to Military-Air.  The 1956 source carried NO per-target category, so
         this is a fallback inferred from the list being airfields
         (category_source = "fallback_airfield_1956").
       - The study assigned NO per-target yield (it is a *requirements* study);
         doctrine was ~1.7-9 MT.  We model 1700 kt (Mk-15/36-class, low end of
         the documented band).
       - BURST: airfields are *soft, area* targets, so they are modeled as an
         optimum-height AIR burst (maximizes the blast area over soft targets),
         consistent with the other soft categories (economic/government/
         population).  Local fallout from an airburst is minimal by design.
       - COORDINATES: the CSV points are city/district centroids (many rows are
         literally "* District"), WGS84, and are ~+/-5-15 km from the true 1956
         airfield DGZ.  Flagged per row via "coord_note".
     Source: NSA Electronic Briefing Book #538; Wellerstein blog.nuclearsecrecy.com

  2. A curated table of well-documented MODERN strategic sites inside these
     countries (ICBM fields, SSBN/bomber bases, C2 bunkers, radars, DPRK nuclear)
     from FAS / Kristensen & Korda "Nuclear Notebook", russianforces.org, and the
     OPEN-RISOP (MIT) NW/OMT/LC3/ECON taxonomy + yield conventions.  Each modern
     row carries a per-site "source_ref" and, where relevant, a modeling "note":
       - Dispersed ICBM/silo FIELDS are represented by a single DGZ; the real
         field disperses many hardened silos over a wide area, so the modeled
         footprint UNDER-represents the counterforce laydown (noted per row).
       - Deep-underground C2 shelters (Yamantau, Kosvinsky Kamen) are hardened
         to survive near-direct hits; the DGZ is a nominal marker, not a
         computed "kill" (noted per row).
       - Modern "yield_kt" is the modeled *attacking counterforce weapon*
         (300-800 kt), NOT the resident warhead yield.

FILTER: keep adversary / Russia-aligned states; exclude now-NATO/EU/US-friendly
countries (Ukraine, Poland, Germany, the Baltics, Georgia, etc.).

OUTPUT: data/targets_adversary.json  (engine schema + provenance fields), plus a
summary to stdout.
"""
import csv, datetime, json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV_IN = os.path.join(ROOT, "prepatory_CSVs", "usa_targets_1956.csv")
OUT    = os.path.join(ROOT, "data", "targets_adversary.json")

NOTEBOOK_EDITION = "Kristensen/Korda/Johns, Nuclear Notebook (FAS), 2023-2024 editions"

# --- country filter --------------------------------------------------------
# Keep adversary / Russia-aligned + China + DPRK; drop now-friendly NATO/EU.
KEEP = {
    "Russia", "China", "North Korea", "Belarus", "Kazakhstan", "Azerbaijan",
    "Armenia", "Turkmenistan", "Uzbekistan", "Kyrgyzstan", "Tajikistan",
    "Republic of Crimea", "Sevastopol",  # Russia-CONTROLLED (see readme.txt)
}
DROP_FRIENDLY = {
    "Ukraine", "Poland", "Germany", "Latvia", "Lithuania", "Estonia",
    "Czech Republic", "Hungary", "Romania", "Bulgaria", "Slovakia", "Albania",
    "Moldova", "Georgia",  # Georgia is aspiring-NATO / pro-West -> excluded
}

# --- name -> category heuristic --------------------------------------------
# NOTE: the 1956 CSV "city" column is bare place/city-centroid names, so these
# keyword rules DO NOT FIRE on the current source (0 of 783 rows match) -- the
# fallback below does 100% of the categorization.  The rules are retained so a
# richer future aimpoint source (with descriptive names) can be dropped in
# without code changes; they are intentionally inert on this input.
KEYWORD_RULES = [
    ("Nuclear",         r"\b(nuclear|atomic|silo|missile|rocket)\b"),
    ("Military-Naval",  r"\b(naval|navy|fleet|submarine|vmb|shipyard|port)\b"),
    ("Command/Control", r"\b(radar|early.?warning|command|bunker|control)\b"),
    ("Economic",        r"\b(refin|power|gres|steel|chemical|aluminum|plant|"
                        r"rail|bridge|dam|gas|oil|industrial)\b"),
    ("Military-Air",    r"\b(air|aero|aviabaza|aerodrome|airfield|airbase|"
                        r"bomber)\b"),
]
# Yield-by-category conventions (kt, burst). Soft/area targets (airfields,
# economic, government, population) use optimum-height AIR bursts; hardened
# counterforce / naval / C2 targets use groundbursts.
YIELD = {
    "Nuclear":         (800.0,  "surface"),
    "Military-Naval":  (500.0,  "surface"),
    "Command/Control": (800.0,  "surface"),
    "Economic":        (300.0,  "air"),
    "Military-Air":    (1700.0, "air"),      # soft area airfield -> airburst
    "Military":        (1700.0, "air"),
    "Government":      (500.0,  "air"),
    "Population":      (500.0,  "air"),
}

def categorize(name):
    low = name.lower()
    for cat, pat in KEYWORD_RULES:
        if re.search(pat, low):
            return cat, "keyword_rule"
    # Source-accurate default: the 1956 list is entirely BRAVO airfields.
    return "Military-Air", "fallback_airfield_1956"

# --- curated modern strategic sites (real coords; sources per row via helper)
# category, name, country, lat, lon, yield_kt, burst
MODERN_SITES = [
    # Russia - ICBM fields (Nuclear / NW), counterforce groundburst
    ("Nuclear", "Kozelsk ICBM field (Yars)", "Russia", 54.03, 35.77, 800, "surface"),
    ("Nuclear", "Tatishchevo ICBM field (Topol-M)", "Russia", 51.686, 45.547, 800, "surface"),
    ("Nuclear", "Uzhur ICBM field (Sarmat/SS-18)", "Russia", 55.114, 89.634, 800, "surface"),
    ("Nuclear", "Dombarovsky ICBM field (Avangard)", "Russia", 50.803, 59.516, 800, "surface"),
    ("Nuclear", "Yoshkar-Ola ICBM base (Yars)", "Russia", 56.6, 47.9, 800, "surface"),
    ("Nuclear", "Nizhny Tagil ICBM base (Yars)", "Russia", 57.9, 59.9, 800, "surface"),
    ("Nuclear", "Novosibirsk ICBM base (Yars)", "Russia", 55.2, 82.9, 800, "surface"),
    ("Nuclear", "Irkutsk ICBM base (Yars)", "Russia", 52.6, 104.3, 800, "surface"),
    ("Nuclear", "Barnaul ICBM base (Yars)", "Russia", 53.4, 83.6, 800, "surface"),
    ("Nuclear", "Teykovo ICBM base (Yars)", "Russia", 56.9, 40.6, 800, "surface"),
    ("Nuclear", "Vypolzovo/Bologoye ICBM base", "Russia", 57.9, 33.6, 800, "surface"),
    # Russia - SSBN / naval
    ("Military-Naval", "Gadzhiyevo SSBN base (Northern Fleet)", "Russia", 69.25, 33.33, 500, "surface"),
    ("Military-Naval", "Rybachiy/Vilyuchinsk SSBN base (Pacific)", "Russia", 52.92, 158.48, 500, "surface"),
    ("Military-Naval", "Severomorsk naval HQ (Northern Fleet)", "Russia", 69.07, 33.42, 500, "surface"),
    ("Military-Naval", "Fokino naval base (Pacific Fleet)", "Russia", 42.96, 132.42, 500, "surface"),
    ("Military-Naval", "Severodvinsk/Sevmash SSBN construction yard", "Russia", 64.58, 39.82, 500, "surface"),
    # Russia - strategic bomber bases (Military-Air), surface (runway cratering)
    ("Military-Air", "Engels-2 strategic bomber base", "Russia", 51.48, 46.21, 800, "surface"),
    ("Military-Air", "Ukrainka strategic bomber base", "Russia", 51.17, 128.45, 800, "surface"),
    ("Military-Air", "Belaya bomber base", "Russia", 52.9, 103.6, 800, "surface"),
    ("Military-Air", "Shaykovka bomber base", "Russia", 54.2, 34.4, 800, "surface"),
    # Russia - C2 / early warning (Command/Control)
    ("Command/Control", "Kosvinsky Kamen alternate command post", "Russia", 59.52, 59.05, 800, "surface"),
    ("Command/Control", "Yamantau leadership shelter", "Russia", 54.27, 58.10, 800, "surface"),
    ("Command/Control", "Armavir early-warning radar", "Russia", 44.9, 41.1, 300, "air"),
    ("Command/Control", "Pionersky early-warning radar", "Russia", 54.9, 20.2, 300, "air"),
    ("Command/Control", "Lekhtusi early-warning radar", "Russia", 60.3, 30.6, 300, "air"),
    ("Government", "Moscow (national command / leadership)", "Russia", 55.7558, 37.6173, 500, "air"),
    # China - silo fields (Nuclear) + SSBN + capital
    ("Nuclear", "Yumen silo field (Gansu)", "China", 40.4, 97.0, 800, "surface"),
    ("Nuclear", "Hami silo field (Xinjiang)", "China", 42.3, 92.7, 800, "surface"),
    ("Nuclear", "Yulin/Ordos silo field", "China", 39.9, 108.5, 800, "surface"),
    ("Nuclear", "Jilantai silo/training field", "China", 39.8, 105.7, 800, "surface"),
    ("Military-Naval", "Longpo/Yulin SSBN base (Hainan)", "China", 18.22, 109.52, 500, "surface"),
    ("Government", "Beijing (national command / leadership)", "China", 39.9042, 116.4074, 500, "air"),
    # North Korea
    ("Nuclear", "Yongbyon nuclear complex", "North Korea", 39.80, 125.75, 500, "surface"),
    ("Nuclear", "Punggye-ri nuclear test site", "North Korea", 41.28, 129.09, 500, "surface"),
    ("Military-Air", "Sohae satellite launch (Tongchang-ri)", "North Korea", 39.66, 124.71, 500, "surface"),
    ("Military-Naval", "Sinpo South Shipyard (SSB)", "North Korea", 40.03, 128.19, 500, "surface"),
    ("Government", "Pyongyang (national command / leadership)", "North Korea", 39.02, 125.75, 500, "air"),
]

def modern_source_ref(country, category):
    """Per-site citation so a reviewer can trace any single coordinate."""
    if country == "China":
        return "FAS Nuclear Notebook (Chinese nuclear forces, 2023-2024); OPEN-RISOP"
    if country == "North Korea":
        return "FAS Nuclear Notebook (North Korean forces); CSIS Beyond Parallel"
    # Russia
    if category == "Nuclear":
        return "russianforces.org (RVSN); FAS Nuclear Notebook (Russian forces)"
    if category == "Military-Naval":
        return "FAS Nuclear Notebook (Russian forces); russianforces.org (Navy)"
    if category == "Command/Control":
        return "FAS/NRDC open literature; russianforces.org (early warning)"
    return "FAS Nuclear Notebook (Russian forces); OPEN-RISOP"

def site_note(name, category):
    """Honesty caveat about what a single modern DGZ actually represents."""
    low = name.lower()
    if "yamantau" in low or "kosvinsky" in low:
        return ("deep-underground hardened shelter engineered to survive a "
                "near-direct hit; DGZ is a nominal marker, not a computed kill")
    if "field" in low:
        return ("single representative DGZ; the real field disperses many "
                "hardened silos over a wide area, so the modeled footprint "
                "under-represents the counterforce laydown")
    return None

def main():
    if not os.path.exists(CSV_IN):
        print(f"ERROR: {CSV_IN} not found", file=sys.stderr); return 1

    targets = []
    kept = dropped = 0
    with open(CSV_IN, encoding="utf-8-sig", newline="") as f:
        for row in csv.DictReader(f):
            country = (row.get("country") or "").strip()
            if country in DROP_FRIENDLY or country not in KEEP:
                dropped += 1; continue
            name = (row.get("city") or "").strip()
            try:
                lat = float(row["lat"]); lon = float(row["lng"])
            except (KeyError, ValueError):
                continue
            cat, csrc = categorize(name)
            y, burst = YIELD.get(cat, YIELD["Military-Air"])
            targets.append({
                "name": name, "state": country, "category": cat,
                "lat": lat, "lon": lon, "yield_kt": y, "burst": burst,
                "alliance": (row.get("alliance") or "").strip(),
                "source": "1956 SAC Air Power list (NSA EBB#538 / Wellerstein)",
                "source_ref": "blog.nuclearsecrecy.com/misc/targets1956/",
                "category_source": csrc,
                "yield_source": "modeled: 1956 doctrine ~1.7-9 MT, airburst over "
                                "soft airfield (attacking weapon, not any resident yield)",
                "coord_note": "city/district centroid (WGS84), ~+/-5-15 km, "
                              "not a surveyed airfield DGZ",
            })
            kept += 1

    n_modern = 0
    for cat, name, country, lat, lon, y, burst in MODERN_SITES:
        obj = {
            "name": name, "state": country, "category": cat,
            "lat": lat, "lon": lon, "yield_kt": float(y), "burst": burst,
            "alliance": "Modern",
            "source": "FAS/Kristensen Nuclear Notebook; russianforces.org; OPEN-RISOP (MIT)",
            "source_ref": modern_source_ref(country, cat),
            "category_source": "hard_mapped_named_site",
            "yield_source": "modeled ATTACKING counterforce weapon (300-800 kt), "
                            "NOT the resident warhead yield",
        }
        note = site_note(name, cat)
        if note:
            obj["note"] = note
        targets.append(obj)
        n_modern += 1

    for i, t in enumerate(targets, 1):
        targets[i-1] = {"id": i, **{k: v for k, v in t.items() if k != "id"}}

    meta = {
        "generated": datetime.date.today().isoformat(),
        "notebook_edition": NOTEBOOK_EDITION,
        "datum": "WGS84",
        "note": ("Adversary (USSR/China/DPRK) target catalog. 1956-airfield rows "
                 "are city-centroid approximations with modeled category/yield; "
                 "modern rows are named sites with real coordinates and modeled "
                 "attacking yields. See per-row source/category_source/yield_source."),
    }
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump({"count": len(targets), "meta": meta, "targets": targets},
                  f, indent=1)

    from collections import Counter
    cats = Counter(t["category"] for t in targets)
    print(f"Wrote {OUT}")
    print(f"  1956 airfield points kept: {kept}  (dropped now-friendly: {dropped})")
    print(f"  modern strategic sites added: {n_modern}")
    print(f"  total targets: {len(targets)}")
    print(f"  categories: {dict(cats)}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
