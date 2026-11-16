type Props = {
  title: string;
  description?: string;
  checked: boolean;
  disabled: boolean;
  onChange: () => void;
};

export function SettingsToggle({ title, description, checked, disabled, onChange }: Props) {
  const help = useId();
  return <div className="settings-option-row">
    <div>
      <span className="settings-option-title">{title}</span>
      {description && <p id={help} className="settings-option-help">{description}</p>}
    </div>
    <button type="button" className="settings-switch" role="switch" aria-label={title}
      aria-describedby={description ? help : undefined} aria-checked={checked} disabled={disabled}
      onClick={onChange}><span /></button>
  </div>;
}
import { useId } from 'react';

