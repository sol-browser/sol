import { useState } from 'react';
import { RotateCcw } from 'lucide-react';
import type { KeyBinding, SettingsCommand } from '../../../packages/browser-contracts/src';
import { useMessages } from './i18n';

const modifiers = [
  [3, 'Ctrl+Alt'], [6, 'Ctrl+Shift'], [5, 'Alt+Shift'], [7, 'Ctrl+Alt+Shift'],
] as const;
const keys = [
  ...Array.from('ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789', label => ({ code: label.charCodeAt(0), label })),
  ...Array.from({ length: 24 }, (_, i) => ({ code: 112 + i, label: `F${i + 1}` })).filter(({ code }) => code !== 123),
];
const defaultBinding: KeyBinding = { key: 78, ctrl: true, alt: true, shift: false };
const modifierMask = (binding: KeyBinding) => Number(binding.ctrl) * 2 + Number(binding.alt) + Number(binding.shift) * 4;
export function littleSolBindingId(binding = defaultBinding) {
  return `${modifierMask(binding)}:${binding.key}`;
}

export function LittleSolShortcut({ binding = defaultBinding, enabled, disabled, save }: {
  binding?: KeyBinding; enabled: boolean; disabled: boolean; save: (command: SettingsCommand) => Promise<void>;
}) {
  const t = useMessages();
  const [mod, setMod] = useState(modifierMask(binding));
  const [key, setKey] = useState(binding.key);
  const [editing, setEditing] = useState(false);
  const title = t('settings.preference.keyboardLittleSol');
  const label = `${modifiers.find(([value]) => value === modifierMask(binding))?.[1]}+${keys.find(item => item.code === binding.key)?.label}`;
  const change = (next: KeyBinding | null) => void save({ method: 'settings.shortcut', action: 'littleSolGlobal', binding: next });
  return <>
    <div className="settings-shortcut-row">
      <span>{title} <small className="settings-shortcut-scope">{t('settings.preference.keyboardGlobal')}</small></span>
      <button type="button" className="settings-shortcut-key" aria-expanded={editing} disabled={disabled}
        aria-label={t('settings.preference.keyboardChange', { action: title })}
        onClick={() => setEditing(value => !value)}>{label}</button>
      <button type="button" className="settings-shortcut-reset" disabled={disabled || littleSolBindingId(binding) === littleSolBindingId()}
        aria-label={t('settings.preference.keyboardResetAction', { action: title })} onClick={() => change(null)}><RotateCcw size={13} /></button>
    </div>
    {!enabled && <p>{t('settings.preference.keyboardLittleSolDisabled')}</p>}
    {editing && <form className="settings-shortcut-editor" onSubmit={event => {
      event.preventDefault();
      change({ key, ctrl: (mod & 2) !== 0, alt: (mod & 1) !== 0, shift: (mod & 4) !== 0 });
    }}>
      <label>{t('settings.preference.keyboardModifiers')}
        <select value={mod} disabled={disabled} onChange={event => setMod(Number(event.target.value))}>
          {modifiers.map(([value, name]) => <option key={value} value={value}>{name}</option>)}
        </select>
      </label>
      <label>{t('settings.preference.keyboardKey')}
        <select value={key} disabled={disabled} onChange={event => setKey(Number(event.target.value))}>
          {keys.map(({ code, label }) => <option key={code} value={code}>{label}</option>)}
        </select>
      </label>
      <button type="submit" className="settings-secondary" disabled={disabled || `${mod}:${key}` === littleSolBindingId(binding)}>{t('common.save')}</button>
    </form>}
  </>;
}
