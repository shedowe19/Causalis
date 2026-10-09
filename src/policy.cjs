'use strict';
const UI_URL = 'causalis://ui/index.html';
const WORKSPACES = Object.freeze(['personal', 'work', 'homelab', 'private']);
function workspaceId(value) {
  if (!WORKSPACES.includes(value)) throw new Error('Unbekannter Arbeitsbereich.');
  return value;
}
function text(value, max = 4096, required = false) {
  if (typeof value !== 'string' || value.length > max || value.includes('\0') || (required && !value.trim())) {
    throw new Error('Ungültige Eingabe.');
  }
  return value;
}
function trustedEvent(event, contents) {
  if (!contents || contents.isDestroyed() || event?.sender !== contents) return false;
  const frame = event.senderFrame;
  const main = contents.mainFrame;
  return Boolean(frame && main && frame.url === UI_URL && frame.routingId === main.routingId && frame.processId === main.processId);
}
function exactHttpsOrigin(value) {
  try {
    const url = new URL(value);
    if (url.protocol !== 'https:' || url.username || url.password || url.origin === 'null') return null;
    return url.origin;
  } catch { return null; }
}
module.exports = {UI_URL, WORKSPACES, workspaceId, text, trustedEvent, exactHttpsOrigin};
