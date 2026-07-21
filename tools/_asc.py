"""_asc.py - shared ESRI ASCII grid writer for the Damaskino data tools.

The engine reads terrain and population as ESRI ASCII grids in geographic
(lon/lat, EPSG:4326) coordinates with square cells sized in degrees.  This helper
writes that format so fetch_dem.py / prepare_pop.py stay consistent.
"""
import numpy as np


def write_ascii_grid(path, data, xll, yll, cellsize, nodata=-9999, fmt="{:.6g}"):
    """Write a 2-D array `data` (row 0 = north/top) as an ESRI ASCII grid.

    xll/yll are the lower-left corner (degrees), cellsize is degrees/cell.
    """
    data = np.asarray(data)
    nrows, ncols = data.shape
    with open(path, "w") as f:
        f.write(f"ncols {ncols}\n")
        f.write(f"nrows {nrows}\n")
        f.write(f"xllcorner {xll}\n")
        f.write(f"yllcorner {yll}\n")
        f.write(f"cellsize {cellsize}\n")
        f.write(f"NODATA_value {nodata}\n")
        for r in range(nrows):
            row = data[r]
            f.write(" ".join(fmt.format(float(v)) for v in row))
            f.write("\n")
