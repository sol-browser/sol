import { useState } from 'react';
import { useIntl } from 'react-intl';
import { messageDescriptor, useMessages } from './i18n';
import { Icon } from './Icon';
import type { Command, Snapshot } from './bridge';

type Props = {
  littleWindow: NonNullable<Snapshot['littleWindow']>;
  ready: boolean;
  command: (command: Command) => Promise<boolean>;
};

export function LittleWindowSpaceButton({ littleWindow, ready, command }: Props) {
  const intl = useIntl();
  const t = useMessages();
  const [sending, setSending] = useState(false);
  const { destinations, preferredSpace } = littleWindow;
  const destination = destinations.find(space => space.id === preferredSpace)
    ?? destinations.find(space => space.sameProfile) ?? destinations[0];
  const canSend = ready && !sending && !!destination;
  const label = destination ? t('little.open', { name: destination.name }) : t('little.openGeneric');

  async function openInSpace() {
    if (!canSend) return;
    setSending(true);
    try { await command({ method: 'window.sendToSpace', space: destination.id }); }
    finally { setSending(false); }
  }

  return <div className="little-space-actions" role="group" aria-label={t('little.group')}>
    <button type="button" className="icon-button little-space-button" aria-label={label} title={label}
      disabled={!canSend} onClick={() => void openInSpace()}>
      {destination ? intl.formatMessage(messageDescriptor('little.open'), { name: <strong>{destination.name}</strong> }) : label}
    </button>
    <button type="button" className="icon-button little-space-picker" aria-label={t('little.other')} aria-haspopup="menu"
      title={t('little.other')} disabled={!canSend} onClick={event => {
        const { x, y, width, height } = event.currentTarget.getBoundingClientRect();
        void command({ method: 'menu.littleSpaces', x: Math.round(x), y: Math.round(y), width: Math.round(width), height: Math.round(height) });
      }}>
      <Icon name="chevron" />
    </button>
  </div>;
}
