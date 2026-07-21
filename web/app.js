/* Damaskino web frontend: render engine GeoJSON on a MapLibre map, with an
 * optional live WASM engine (build with `make wasm`). Degrades to loading
 * precomputed GeoJSON (from the CLI `--geojson`) when WASM is absent. */
'use strict';

const EMPTY_FC = { type: 'FeatureCollection', features: [] };

// Offline-capable style: solid background, no external tiles required.
const style = {
  version: 8,
  sources: {},
  layers: [{ id: 'bg', type: 'background', paint: { 'background-color': '#0b0d14' } }]
};

const map = new maplibregl.Map({
  container: 'map', style, center: [-77.0364, 38.8951], zoom: 8, attributionControl: false
});
map.addControl(new maplibregl.NavigationControl(), 'bottom-right');
map.addControl(new maplibregl.AttributionControl({ compact: true,
  customAttribution: 'Damaskino · effects estimate, not operational' }), 'bottom-right');

let wasm = null; // { geojson: (scnJson, pop, pf) => string }

function setStatus(msg) { document.getElementById('status').textContent = msg || ''; }

function addBasemap() {
  if (map.getSource('osm')) return;
  map.addSource('osm', {
    type: 'raster', tileSize: 256,
    tiles: ['https://tile.openstreetmap.org/{z}/{x}/{y}.png'],
    attribution: '© OpenStreetMap contributors'
  });
  map.addLayer({ id: 'osm', type: 'raster', source: 'osm' }, 'fallout-fill');
}
function removeBasemap() {
  if (map.getLayer('osm')) map.removeLayer('osm');
  if (map.getSource('osm')) map.removeSource('osm');
}

function installLayers() {
  map.addSource('dmk', { type: 'geojson', data: EMPTY_FC });

  // Fallout dose fill (H+1 R/hr), color-graded.
  map.addLayer({
    id: 'fallout-fill', type: 'fill', source: 'dmk',
    filter: ['==', ['get', 'kind'], 'fallout'],
    paint: {
      'fill-color': ['interpolate', ['linear'], ['get', 'dose_rate_rhr'],
        1, '#2b3a67', 10, '#3d6d9e', 100, '#e2c044', 1000, '#e07b1b', 10000, '#e0201b'],
      'fill-opacity': 0.55
    }
  });

  // Effect rings (line outlines) by kind.
  const ring = (id, kind, color) => map.addLayer({
    id, type: 'line', source: 'dmk',
    filter: ['==', ['get', 'kind'], kind],
    paint: { 'line-color': color, 'line-width': 2, 'line-opacity': 0.9 }
  });
  ring('blast-rings', 'blast', '#ff3b30');
  ring('thermal-rings', 'thermal', '#ff9500');
  ring('prompt-rings', 'prompt_radiation', '#bf5af2');

  // Ground zero marker.
  map.addLayer({
    id: 'gz', type: 'circle', source: 'dmk',
    filter: ['==', ['get', 'kind'], 'ground_zero'],
    paint: { 'circle-radius': 6, 'circle-color': '#ffffff', 'circle-stroke-color': '#000', 'circle-stroke-width': 2 }
  });

  // Popups.
  const popup = new maplibregl.Popup({ closeButton: false });
  map.on('click', (e) => {
    const f = map.queryRenderedFeatures(e.point,
      { layers: ['fallout-fill', 'blast-rings', 'thermal-rings', 'prompt-rings', 'gz'] })[0];
    if (!f) { popup.remove(); return; }
    const p = f.properties, html = [];
    if (p.kind === 'ground_zero') html.push(`<b>Ground zero</b><br>${p.name || ''}<br>${(+p.yield_kt).toLocaleString()} kt`);
    else if (p.kind === 'fallout') html.push(`<b>Fallout</b><br>${(+p.dose_rate_rhr).toFixed(0)} R/hr (H+1)`);
    else html.push(`<b>${p.label || p.kind}</b>${p.radius_km ? '<br>' + (+p.radius_km).toFixed(2) + ' km' : ''}`);
    popup.setLngLat(e.lngLat).setHTML(html.join('')).addTo(map);
  });
  ['fallout-fill', 'blast-rings', 'thermal-rings', 'prompt-rings', 'gz'].forEach(l => {
    map.on('mouseenter', l, () => map.getCanvas().style.cursor = 'pointer');
    map.on('mouseleave', l, () => map.getCanvas().style.cursor = '');
  });
}

function fitTo(fc) {
  const b = new maplibregl.LngLatBounds();
  let any = false;
  for (const f of fc.features || []) {
    const g = f.geometry; if (!g) continue;
    const walk = (c) => {
      if (typeof c[0] === 'number') { b.extend(c); any = true; }
      else c.forEach(walk);
    };
    walk(g.coordinates);
  }
  if (any) map.fitBounds(b, { padding: 60, maxZoom: 12, duration: 400 });
}

function render(fc) {
  if (!fc || fc.error) { setStatus('Error: ' + (fc && fc.error || 'no data')); return; }
  map.getSource('dmk').setData(fc);
  fitTo(fc);
  const n = (fc.features || []).length;
  setStatus(`${n.toLocaleString()} features`);
}

function currentScenario() {
  const wind = (document.getElementById('wind').value || '15@270').split('@');
  return {
    location: { name: 'Custom', lat: +document.getElementById('lat').value, lon: +document.getElementById('lon').value },
    weapon: { yield_kt: +document.getElementById('yield').value, burst: document.getElementById('burst').value },
    wind: { surface: { speed_kts: +wind[0] || 0, direction_deg: +wind[1] || 270 } },
    grid: { n: 220, cell_km: 1.0, ref_time_hr: 1.0 }
  };
}

function runScenario() {
  if (!wasm) {
    setStatus('Live engine not built — run `make wasm`, or load CLI GeoJSON below.');
    return;
  }
  setStatus('Running…');
  try {
    const scn = JSON.stringify(currentScenario());
    const pop = +document.getElementById('pop').value || 0;
    const gj = wasm.geojson(scn, pop, 1);
    render(JSON.parse(gj));
  } catch (err) { setStatus('Engine error: ' + err); }
}

// Wire UI once the map is ready.
map.on('load', () => {
  installLayers();

  const vis = (id, on) => map.getLayer(id) && map.setLayoutProperty(id, 'visibility', on ? 'visible' : 'none');
  document.getElementById('t-fallout').onchange = e => vis('fallout-fill', e.target.checked);
  document.getElementById('t-blast').onchange = e => vis('blast-rings', e.target.checked);
  document.getElementById('t-thermal').onchange = e => vis('thermal-rings', e.target.checked);
  document.getElementById('t-prompt').onchange = e => vis('prompt-rings', e.target.checked);
  document.getElementById('t-base').onchange = e => e.target.checked ? addBasemap() : removeBasemap();
  document.getElementById('run').onclick = runScenario;
  document.getElementById('file').onchange = (e) => {
    const file = e.target.files[0]; if (!file) return;
    const r = new FileReader();
    r.onload = () => { try { render(JSON.parse(r.result)); } catch (err) { setStatus('Bad GeoJSON: ' + err); } };
    r.readAsText(file);
  };

  // Try to load the live WASM engine; else load the precomputed example.
  loadWasm().then(ok => {
    if (ok) { setStatus('Live engine ready.'); runScenario(); }
    else {
      fetch('example.geojson').then(r => r.ok ? r.json() : Promise.reject())
        .then(render)
        .catch(() => setStatus('No example.geojson — run `make example` or load a file.'));
    }
  });
});

async function loadWasm() {
  if (typeof Damaskino !== 'function') {
    // Load the emscripten module if it was built (make wasm); ignore if absent.
    await new Promise((res) => {
      const s = document.createElement('script');
      s.src = 'damaskino.js'; s.onload = res; s.onerror = res;
      document.head.appendChild(s);
    });
  }
  if (typeof Damaskino !== 'function') return false;
  try {
    const mod = await Damaskino();
    const fn = mod.cwrap('dmk_web_geojson', 'number', ['string', 'number', 'number']);
    const free = mod.cwrap('dmk_web_free', null, ['number']);
    wasm = {
      geojson: (scn, pop, pf) => {
        const ptr = fn(scn, pop, pf);
        const s = mod.UTF8ToString(ptr);
        free(ptr);
        return s;
      }
    };
    return true;
  } catch (e) { return false; }
}
