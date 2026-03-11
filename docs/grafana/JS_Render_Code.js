// ============================================================
// ACE.SVG — RENDER FUNCTION v3 (COMPLETE)
// AetherLogic Floor Plan — ELVIAL
// Paste this in the "User JS Render" tab
// ============================================================

const { NODE_CONFIG, THRESHOLDS, COLORS, DASHBOARD_UID, DASHBOARD_SLUG } = context;

// -----------------------------------------------------------
// HELPERS
// -----------------------------------------------------------
function getNodeColor(iaq) {
  if (iaq == null || isNaN(iaq)) return COLORS.offline;
  if (iaq > THRESHOLDS.iaq.danger)  return COLORS.critical;
  if (iaq > THRESHOLDS.iaq.warning) return COLORS.danger;
  if (iaq > THRESHOLDS.iaq.ok)      return COLORS.warning;
  return COLORS.ok;
}

function isCritical(row) {
  if (!row) return false;
  return (row.iaq > THRESHOLDS.iaq.danger) ||
         (row.co2 > THRESHOLDS.co2.danger) ||
         (row.pm25 > THRESHOLDS.pm25.danger) ||
         (row.pm10 > THRESHOLDS.pm10.danger) ||
         (row.noise > THRESHOLDS.noise.danger);
}

function isWarning(row) {
  if (!row) return false;
  return (row.iaq > THRESHOLDS.iaq.ok) ||
         (row.co2 > THRESHOLDS.co2.ok) ||
         (row.pm25 > THRESHOLDS.pm25.ok) ||
         (row.pm10 > THRESHOLDS.pm10.ok) ||
         (row.noise > THRESHOLDS.noise.ok);
}

function fmt(val, decimals = 0) {
  if (val == null || isNaN(val)) return '--';
  return Number(val).toFixed(decimals);
}

function getFieldValue(field, index) {
  if (!field) return null;
  const vals = field.values.toArray ? field.values.toArray() :
               (Array.isArray(field.values) ? field.values : []);
  return vals[index] !== undefined ? vals[index] : null;
}

// -----------------------------------------------------------
// PARSE DATA — Multiple series (one per node)
// -----------------------------------------------------------
let nodeData = {};

try {
  if (data.series && data.series.length > 0) {
    console.log('[DEBUG] Total series:', data.series.length);

    data.series.forEach((series, idx) => {
      const fields = series.fields;
      if (!fields || fields.length === 0) return;

      // --- Find the node ID ---
      let nodeId = '';

      // Method 1: 'id' field in data
      const idField = fields.find(f => f.name === 'id');
      if (idField) {
        nodeId = getFieldValue(idField, 0) || '';
        if (nodeId) console.log('[DEBUG] Series', idx, '→ id from field:', nodeId);
      }

      // Method 2: labels on first field
      if (!nodeId && fields[0] && fields[0].labels) {
        nodeId = fields[0].labels.id || '';
        if (nodeId) console.log('[DEBUG] Series', idx, '→ id from labels:', nodeId);
      }

      // Method 3: series name
      if (!nodeId && series.name) {
        nodeId = series.name;
        console.log('[DEBUG] Series', idx, '→ id from name:', nodeId);
      }

      // Method 4: series refId or meta
      if (!nodeId && series.refId) {
        nodeId = series.refId;
        console.log('[DEBUG] Series', idx, '→ id from refId:', nodeId);
      }

      if (!nodeId) {
        console.log('[DEBUG] Series', idx, '→ NO ID FOUND. Fields:', fields.map(f => f.name), 'Labels:', fields[0]?.labels);
        return;
      }

      // --- Build data row ---
      const row = {};
      fields.forEach(f => {
        if (f.name === 'id') return;
        const val = getFieldValue(f, 0);
        if (val !== null) row[f.name] = val;
      });

      // --- Map field names ---
      if (row.v !== undefined && row.bat === undefined) row.bat = row.v;
      if (row.db !== undefined && row.noise === undefined) row.noise = row.db;

      if (Object.keys(row).length > 0) {
        nodeData[nodeId] = row;
      }
    });
  }
} catch (e) {
  console.error('[AetherLogic] Data parse error:', e);
}

console.log('[AetherLogic] Parsed nodes:', Object.keys(nodeData));
console.log('[AetherLogic] Node data:', JSON.stringify(nodeData));

// -----------------------------------------------------------
// TOOLTIP — HTML overlay on document.body (fixed position)
// -----------------------------------------------------------
function showTooltip(nodeId, row, svgElement) {
  hideTooltip();

  const iaqColor = row.iaq > 150 ? '#F2495C' :
                   row.iaq > 100 ? '#FF9830' :
                   row.iaq > 50  ? '#F2CC0C' : '#73BF69';

  const tip = document.createElement('div');
  tip.id = 'aetherlogic-tooltip';
  tip.style.cssText = [
    'position:fixed',
    'background:#1e2a3a',
    'border:1px solid #4a6fa5',
    'border-radius:8px',
    'padding:12px 15px',
    'color:#e8eaf6',
    'font-family:Segoe UI,Arial,sans-serif',
    'font-size:12px',
    'line-height:1.8',
    'z-index:99999',
    'pointer-events:none',
    'box-shadow:0 4px 20px rgba(0,0,0,0.5)',
    'min-width:210px',
    'max-width:260px'
  ].join(';');

  tip.innerHTML =
    '<div style="font-weight:700;color:#8ab4f8;font-size:14px;margin-bottom:6px;border-bottom:1px solid #4a6fa5;padding-bottom:5px;">' +
      '📍 ' + nodeId +
    '</div>' +
    '<div>🌡️ Θερμοκρασία: <b>' + fmt(row.temp, 1) + ' °C</b></div>' +
    '<div>💧 Υγρασία: <b>' + fmt(row.hum, 1) + ' %</b></div>' +
    '<div>💨 CO₂: <b>' + fmt(row.co2) + ' ppm</b></div>' +
    '<div>🫁 PM2.5: <b>' + fmt(row.pm25, 1) + ' μg/m³</b></div>' +
    '<div>🫁 PM10: <b>' + fmt(row.pm10, 1) + ' μg/m³</b></div>' +
    '<div style="color:' + iaqColor + '">📊 IAQ: <b>' + fmt(row.iaq) + '</b></div>' +
    '<div>🔊 Θόρυβος: <b>' + fmt(row.noise, 1) + ' dB</b></div>' +
    '<div style="margin-top:6px;border-top:1px solid #4a6fa5;padding-top:5px;color:#90a4ae;font-size:11px;">' +
      '🔋 ' + fmt(row.bat, 2) + ' V &nbsp;│&nbsp; 📶 ' + fmt(row.rssi) + ' dBm' +
    '</div>' +
    '<div style="color:#607d8b;font-size:10px;margin-top:4px;">Κλικ για αναλυτικά →</div>';

  document.body.appendChild(tip);

  // Position near the SVG element using getBoundingClientRect
  try {
    const el = svgElement.node ? svgElement.node : svgElement;
    const rect = el.getBoundingClientRect();
    let left = rect.right + 12;
    let top = rect.top - 40;

    // Keep within viewport
    if (left + 270 > window.innerWidth) left = rect.left - 270;
    if (top + 300 > window.innerHeight) top = window.innerHeight - 310;
    if (top < 10) top = 10;

    tip.style.left = left + 'px';
    tip.style.top = top + 'px';
  } catch (e) {
    console.warn('[AetherLogic] Tooltip position error:', e);
    tip.style.left = '50%';
    tip.style.top = '30%';
  }

  console.log('[DEBUG] Tooltip shown for:', nodeId);
}

function hideTooltip() {
  const el = document.getElementById('aetherlogic-tooltip');
  if (el) el.remove();
}

// -----------------------------------------------------------
// UPDATE EACH NODE
// -----------------------------------------------------------
let nodesOnline = 0;
let activeAlerts = 0;

Object.entries(NODE_CONFIG).forEach(([nodeId, cfg]) => {
  const sfx = cfg.suffix;
  const row = nodeData[nodeId];

  const circle    = svgmap['circle_' + sfx];
  const valPm25   = svgmap['val_pm25_' + sfx];
  const valPm10   = svgmap['val_pm10_' + sfx];
  const valNoise  = svgmap['val_noise_' + sfx];
  const link      = svgmap['link_' + sfx];
  const nodeGroup = svgmap['node_' + sfx];

  // Debug: check if svgmap elements exist
  if (!nodeGroup) {
    console.warn('[DEBUG] svgmap missing node_' + sfx);
  }

  if (!row) {
    if (circle)   circle.fill(COLORS.offline);
    if (valPm25)  valPm25.text('PM2.5: --');
    if (valPm10)  valPm10.text('PM10: --');
    if (valNoise) valNoise.text('🔊 -- dB');
    if (link)     link.stroke(COLORS.offline);
    console.log('[DEBUG]', nodeId, '→ OFFLINE (no data)');
    return;
  }

  nodesOnline++;
  console.log('[DEBUG]', nodeId, '→ ONLINE', row);

  // --- UPDATE VALUES ---
  if (valPm25)  valPm25.text('PM2.5: ' + fmt(row.pm25, 1));
  if (valPm10)  valPm10.text('PM10: ' + fmt(row.pm10, 1));
  if (valNoise) valNoise.text('🔊 ' + fmt(row.noise, 1) + ' dB');

  // --- COLOR based on IAQ ---
  const color = getNodeColor(row.iaq);
  if (circle) circle.fill(color);

  // --- LoRa LINK color based on RSSI ---
  if (link) {
    let linkColor = COLORS.ok;
    if (row.rssi != null) {
      if (row.rssi < -110)     linkColor = COLORS.critical;
      else if (row.rssi < -90) linkColor = COLORS.danger;
      else if (row.rssi < -70) linkColor = COLORS.warning;
    }
    link.stroke(linkColor);
  }

  // --- FLASH ANIMATION for critical ---
  if (circle) {
    if (isCritical(row)) {
      activeAlerts++;
      circle.addClass('critical-pulse');
      circle.attr('filter', 'url(#glow-red)');
    } else {
      circle.removeClass('critical-pulse');
      if (isWarning(row)) {
        circle.attr('filter', '');
      } else {
        circle.attr('filter', 'url(#glow-green)');
      }
    }
  }

  // --- HOVER TOOLTIP ---
  if (nodeGroup) {
    // Remove old handlers to avoid stacking
    nodeGroup.off('mouseenter');
    nodeGroup.off('mouseleave');

    nodeGroup.on('mouseenter', function() {
      console.log('[DEBUG] Mouse enter:', nodeId);
      showTooltip(nodeId, row, circle || nodeGroup);
    });
    nodeGroup.on('mouseleave', function() {
      console.log('[DEBUG] Mouse leave:', nodeId);
      hideTooltip();
    });
  }

  // --- CLICK → Navigate to main dashboard (new tab) ---
  if (nodeGroup) {
    nodeGroup.off('click');
    nodeGroup.on('click', function() {
      console.log('[DEBUG] Click:', nodeId);
      const baseUrl = window.location.origin;
      const timeParams = '&from=' + (new Date(Date.now() - 6 * 3600000)).toISOString()
                       + '&to=' + new Date().toISOString();
      const url = baseUrl + '/d/' + DASHBOARD_UID + '/' + DASHBOARD_SLUG
                + '?var-NodeID=' + encodeURIComponent(nodeId) + timeParams;
      window.open(url, '_blank');
    });
    nodeGroup.css('cursor', 'pointer');
  }
});

// -----------------------------------------------------------
// SYSTEM STATUS BOX
// -----------------------------------------------------------
if (svgmap.status_nodes_online) {
  svgmap.status_nodes_online.text('● Κόμβοι Online: ' + nodesOnline + '/5');
  svgmap.status_nodes_online.fill(
    nodesOnline >= 5 ? COLORS.ok :
    nodesOnline >= 3 ? COLORS.warning : COLORS.critical
  );
}
if (svgmap.status_gw) {
  const gwOnline = nodesOnline > 0;
  svgmap.status_gw.text('● Gateway: ' + (gwOnline ? 'Online' : 'Offline'));
  svgmap.status_gw.fill(gwOnline ? COLORS.ok : COLORS.critical);
}
if (svgmap.status_alerts) {
  svgmap.status_alerts.text('● Alerts: ' + activeAlerts + ' active');
  svgmap.status_alerts.fill(activeAlerts === 0 ? COLORS.ok : COLORS.critical);
}
if (svgmap.status_update) {
  const now = new Date();
  const timeStr = now.toLocaleTimeString('el-GR', {
    hour: '2-digit', minute: '2-digit', second: '2-digit'
  });
  svgmap.status_update.text('Τελ. ενημέρωση: ' + timeStr);
}

// -----------------------------------------------------------
// CLEANUP
// -----------------------------------------------------------
hideTooltip();

console.log('[AetherLogic] Render complete. Online:', nodesOnline, '/ Alerts:', activeAlerts);
console.log('[DEBUG] svgmap keys:', Object.keys(svgmap));
