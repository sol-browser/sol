/** The update window's state (native `update.state`) and what it says about it. */
export const UPDATE_PHASES = ['idle', 'checking', 'downloading', 'ready', 'upToDate', 'failed', 'notInstalled'] as const;
export type UpdatePhase = typeof UPDATE_PHASES[number];
export interface UpdateState { phase: UpdatePhase; current: string; version: string; percent: number }

export function parseUpdateState(raw: string): UpdateState {
  const value: unknown = JSON.parse(raw);
  if (typeof value !== 'object' || value === null || Array.isArray(value)) throw new Error('Invalid update state');
  const { phase, current, version, percent } = value as Record<string, unknown>;
  if (typeof phase !== 'string' || !(UPDATE_PHASES as readonly string[]).includes(phase) || typeof current !== 'string' ||
      typeof version !== 'string' || typeof percent !== 'number' || !Number.isInteger(percent) || percent < 0 || percent > 100)
    throw new Error('Invalid update state');
  return { phase: phase as UpdatePhase, current: current.slice(0, 32), version: version.slice(0, 32), percent };
}

export interface UpdateView {
  title: string;
  detail: string;
  action: 'restart' | 'retry' | null;
}

/** The words of each phase, in the web installer's voice (apps/installer/src/view.cpp). */
export function updateView(state: UpdateState): UpdateView {
  const next = state.version ? `Sol ${state.version}` : 'the update';
  switch (state.phase) {
    case 'downloading':
      return { title: `Downloading ${next}`, detail: state.percent >= 100 ? 'Almost done…' : `${next}  ·  ${state.percent}%  ·  keep browsing`, action: null };
    case 'ready':
      return { title: `${next} is ready`, detail: 'Restart Sol to finish. It also installs the next time Sol starts.', action: 'restart' };
    case 'upToDate':
      return { title: 'Sol is up to date', detail: `You have the latest version, ${state.current}.`, action: null };
    case 'failed':
      return { title: 'Couldn’t check for updates', detail: 'Check your connection and try again.', action: 'retry' };
    case 'notInstalled':
      return { title: 'This copy does not update itself', detail: 'Updates arrive with the installed Sol, which downloads them in the background.', action: null };
    default:
      return { title: 'Checking for updates', detail: `Looking for a version newer than ${state.current}…`, action: null };
  }
}

/** Where the mark is heading (sol-mark.ts): the ring fills as the download progresses, then the water sinks into the logo. */
export function markTarget(state: UpdateState): { fill: number; morph: number; done: boolean } {
  switch (state.phase) {
    case 'downloading': return { fill: 0.05 + 0.95 * state.percent / 100, morph: 0, done: false };
    case 'ready':
    case 'upToDate': return { fill: 1, morph: 1, done: true };
    case 'failed':
    case 'notInstalled': return { fill: 0, morph: 0, done: false };
    default: return { fill: 0.05, morph: 0, done: false };
  }
}
