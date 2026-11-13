import { useState } from 'react';
import { Icon } from './Icon';
import type { Command, Snapshot } from './bridge';

type Props = {
  littleWindow: NonNullable<Snapshot['littleWindow']>;
  ready: boolean;
  command: (command: Command) => Promise<boolean>;
};

export function LittleWindowSpaceButton({ littleWindow, ready, command }: Props) {
  const [sending, setSending] = useState(false);
  const { destinations, preferredSpace } = littleWindow;
  const destination = destinations.find(space => space.id === preferredSpace)
    ?? destinations.find(space => space.sameProfile) ?? destinations[0];
  const canSend = ready && !sending && !!destination;
  const label = `Open in ${destination?.name ?? 'Space'}`;

  async function openInSpace() {
    if (!canSend) return;
    setSending(true);
    try { await command({ method: 'window.sendToSpace', space: destination.id }); }
    finally { setSending(false); }
  }

  return <div className="little-space-actions" role="group" aria-label="Send page to a Space">
    <button type="button" className="icon-button little-space-button" aria-label={label} title={label}
      disabled={!canSend} onClick={() => void openInSpace()}>
      Open in <strong>{destination?.name ?? 'Space'}</strong>
    </button>
    <button type="button" className="icon-button little-space-picker" aria-label="Open in another Space" aria-haspopup="menu"
      title="Open in another Space" disabled={!canSend} onClick={event => {
        const { x, y, width, height } = event.currentTarget.getBoundingClientRect();
        void command({ method: 'menu.littleSpaces', x: Math.round(x), y: Math.round(y), width: Math.round(width), height: Math.round(height) });
      }}>
      <Icon name="chevron" />
    </button>
  </div>;
}
