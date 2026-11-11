// State of Sol's separate DevTools window strip (windows/devtools_window.cpp Payload()).
export type DevToolsWindowState = { target: number; attached: boolean; device: boolean; deviceAvailable: boolean };

export function parseDevToolsWindowState(raw: string): DevToolsWindowState {
  const v = JSON.parse(raw) as Record<string, unknown>;
  if (typeof v !== 'object' || v === null || !Number.isSafeInteger(v.target) || typeof v.attached !== 'boolean' ||
      typeof v.device !== 'boolean' || typeof v.deviceAvailable !== 'boolean') throw new Error('Invalid developer tools state');
  return { target: v.target as number, attached: v.attached, device: v.device, deviceAvailable: v.deviceAvailable };
}

