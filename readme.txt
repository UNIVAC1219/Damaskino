Data sources & attribution
==========================

Large list of targets in USA is from https://www.nuclearwarmap.com/targetlist.html
  -> cleaned by tools/build_targets.py into data/targets.json

Large list of targets in China/former USSR is from https://blog.nuclearsecrecy.com/misc/targets1956/
  (1956 SAC "Atomic Weapons Requirements Study", Air Power airfield list)
  -> raw CSV: prepatory_CSVs/usa_targets_1956.csv
  -> filtered/categorized/yield-modeled by tools/build_adversary_targets.py
     into data/targets_adversary.json (see that file's provenance fields)
  NOTE: the 1956 airfield points are city/district centroids (WGS84), roughly
  +/-5-15 km from the true airfield DGZ, not surveyed aimpoints. Category and
  yield are modeled (the source study specified neither); airfields are modeled
  as soft-area AIR bursts. Crimea and Sevastopol are included on a CONTROL basis
  (Russia-controlled since 2014) for force-laydown purposes; this is not a
  sovereignty judgment (both are internationally recognized as Ukraine).

Small list is from https://worldpopulationreview.com/state-rankings/nuclear-targets-by-state

WSEG-10 report from https://apps.dtic.mil/sti/tr/pdf/AD0261752.pdf

Topographic data from https://portal.opentopography.org/raster?opentopoID=OTSRTM.122019.4326.1

Modern strategic-site coordinates/yields (in data/targets_adversary.json) are from
FAS / Kristensen & Korda "Nuclear Notebook", russianforces.org, and the MIT-licensed
OPEN-RISOP target taxonomy. All yields there are modeled from public doctrine, not
classified planning documents.

License applies to all content except topographic tiles and the WSEG-10 PDF.
