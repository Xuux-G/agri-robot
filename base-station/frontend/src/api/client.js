export const apiBase = (import.meta.env.VITE_API_BASE || "").replace(/\/$/, "");
const writeToken = import.meta.env.VITE_DEVICE_TOKEN || "";

function writeHeaders(extra = {}) {
  return writeToken ? { ...extra, "X-Device-Token": writeToken } : extra;
}

async function handleResponse(res) {
  if (res.ok) return res.json();
  const text = await res.text();
  let detail = text;
  try {
    const json = JSON.parse(text);
    detail = json.detail || json.message || text;
  } catch (_) { /* not JSON, use raw text */ }
  throw new Error(`HTTP ${res.status}: ${detail}`);
}

export { handleResponse };

export function getOverview() {
  return fetch(`${apiBase}/api/dashboard/overview`).then(handleResponse);
}

export function getPlotHistory(plotCode) {
  const path = `/api/plots/${encodeURIComponent(plotCode)}/history`;
  return fetch(`${apiBase}${path}`).then(handleResponse);
}

export function getRecentDetections(limit = 20) {
  return fetch(`${apiBase}/api/detections/recent?limit=${limit}`).then(handleResponse);
}

export function createDetection(payload) {
  return fetch(`${apiBase}/api/detections`, {
    method: "POST",
    headers: writeHeaders({ "Content-Type": "application/json" }),
    body: JSON.stringify(payload),
  }).then(handleResponse);
}

export function updateDetection(id, payload) {
  return fetch(`${apiBase}/api/detections/${id}`, {
    method: "PUT",
    headers: writeHeaders({ "Content-Type": "application/json" }),
    body: JSON.stringify(payload),
  }).then(handleResponse);
}

export function deleteDetection(id) {
  return fetch(`${apiBase}/api/detections/${id}`, {
    method: "DELETE",
    headers: writeHeaders(),
  }).then(handleResponse);
}

export function getFarmStatus() {
  return fetch(`${apiBase}/api/farm/status`).then(handleResponse);
}

export function getFarmPlots() {
  return fetch(`${apiBase}/api/farm/plots`).then(handleResponse);
}

export function getFarmPlotDetail(blockId) {
  const path = `/api/farm/plots/${encodeURIComponent(blockId)}/detail`;
  return fetch(`${apiBase}${path}`).then(handleResponse);
}

export function getFarmPlotPrescription(blockId) {
  const path = `/api/farm/plots/${encodeURIComponent(blockId)}/prescription`;
  return fetch(`${apiBase}${path}`).then(handleResponse);
}
export function getFarmWeather() {
  return fetch(`${apiBase}/api/farm/weather`).then(handleResponse);
}
export function getFarmRobot() {
  return fetch(`${apiBase}/api/farm/robot`).then(handleResponse);
}
export function clearSimulation() {
  return fetch(`${apiBase}/api/sim/clear`, {
    method: "POST",
    headers: writeHeaders(),
  }).then(handleResponse);
}
export function clearFarmPlot(blockId) {
  const path = `/api/farm/plots/${encodeURIComponent(blockId)}`;
  return fetch(`${apiBase}${path}`, {
    method: "DELETE",
    headers: writeHeaders(),
  }).then(handleResponse);
}
export function patchDeviceEvent(eventId, payload) {
  return fetch(`${apiBase}/api/device/events/${encodeURIComponent(eventId)}`, {
    method: "PATCH",
    headers: writeHeaders({ "Content-Type": "application/json" }),
    body: JSON.stringify(payload),
  }).then(handleResponse);
}
export function deleteDeviceEvent(eventId) {
  return fetch(`${apiBase}/api/device/events/${encodeURIComponent(eventId)}`, {
    method: "DELETE",
    headers: writeHeaders(),
  }).then(handleResponse);
}

export function askAIAdvise(question) {
  return fetch(`${apiBase}/api/ai/advise`, {
    method: "POST",
    headers: writeHeaders({ "Content-Type": "application/json" }),
    body: JSON.stringify({ question }),
  }).then(handleResponse);
}
