// ============================================================
// ACE.SVG — INIT FUNCTION
// AetherLogic Floor Plan — ELVIAL
// Paste this in the "User JS Init" tab
// ============================================================

// Dashboard URL for navigation on click
const DASHBOARD_UID = 'aetherlogic-main-fixed2';
const DASHBOARD_SLUG = 'aetherlogic-main-dashboard';

// Node ID mapping: InfluxDB id tag → SVG element suffixes
const NODE_CONFIG = {
  'Κάθετο 1α': { suffix: 'k1a', cx: 180, cy: 205 },
  'Κάθετο 1β': { suffix: 'k1b', cx: 410, cy: 205 },
  'Κάθετο 2':  { suffix: 'k2',  cx: 640, cy: 205 },
  'Διάδρομος':  { suffix: 'dia', cx: 410, cy: 365 },
  'Αφρός Ι2':  { suffix: 'afr', cx: 180, cy: 585 },
};

// IAQ thresholds (based on your Grafana alert rules)
const THRESHOLDS = {
  iaq:   { ok: 50, warning: 100, danger: 150 },
  co2:   { ok: 800, warning: 1000, danger: 1500 },
  pm25:  { ok: 25, warning: 50, danger: 100 },
  pm10:  { ok: 50, warning: 100, danger: 150 },
  noise: { ok: 75, warning: 80, danger: 85 },
};

// Colors
const COLORS = {
  ok:       '#73BF69',
  warning:  '#F2CC0C',
  danger:   '#FF9830',
  critical: '#F2495C',
  offline:  '#546E7A',
};

// Store config in context for use in Render
context.DASHBOARD_UID = DASHBOARD_UID;
context.DASHBOARD_SLUG = DASHBOARD_SLUG;
context.NODE_CONFIG = NODE_CONFIG;
context.THRESHOLDS = THRESHOLDS;
context.COLORS = COLORS;
context.activeAnimations = {};

console.log('[AetherLogic] Init complete. Nodes configured:', Object.keys(NODE_CONFIG));
