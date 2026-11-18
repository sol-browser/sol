import { useRef, useState } from 'react';
import { ArrowDown, ArrowUp, Plus, Trash2 } from 'lucide-react';
import { maxLinkPatternBytes, maxLinkRoutes, validLinkRouting, type LinkRoute, type LinkRouting,
  type SpaceInfo } from '../../../packages/browser-contracts/src';
import { useMessages } from './i18n';
import { focusOnMount } from './focus-on-mount';

type Props = { routing: LinkRouting; spaces: SpaceInfo[]; disabled: boolean; save: (routing: LinkRouting) => Promise<void> };
type RuleDraft = LinkRoute & { id: number };
export const linkRoutePlaceholders: Record<LinkRoute['match'], string> = {
  contains: 'example.com/team/', exact: 'https://example.com/page', regex: String.raw`example\.com/team/\d+$`,
};

export function LinkDestinationOptions({ selected, spaces }: { selected: string; spaces: SpaceInfo[] }) {
  const t = useMessages();
  return <>
    <option value="littleSol">Little Sol</option>
    <option value="recent">{t('settings.routes.recent')}</option>
    {[...spaces].sort((a, b) => a.position - b.position).map(space => <option key={space.id} value={space.id}>{space.name}</option>)}
    {selected !== 'recent' && selected !== 'littleSol' && !spaces.some(space => space.id === selected) &&
      <option value={selected} disabled>{t('settings.routes.missing')}</option>}
  </>;
}

export function LinkRoutingSettings({ routing, spaces, disabled, save }: Props) {
  const t = useMessages();
  const [rules, setRules] = useState<RuleDraft[]>(() => routing.rules.map((rule, id) => ({ ...rule, id })));
  const [fallback, setFallback] = useState(routing.fallback);
  const [added, setAdded] = useState<number>();
  const nextId = useRef(routing.rules.length);
  const draft = { rules: rules.map(({ match, pattern, destination }) => ({ match, pattern: pattern.replace(/^ +| +$/gu, ''), destination })), fallback };
  const valid = validLinkRouting(draft);
  const dirty = fallback !== routing.fallback || draft.rules.length !== routing.rules.length || draft.rules.some((rule, index) => {
    const saved = routing.rules[index];
    return rule.match !== saved.match || rule.pattern !== saved.pattern || rule.destination !== saved.destination;
  });
  function update(id: number, change: Partial<LinkRoute>) {
    setRules(current => current.map(rule => rule.id === id ? { ...rule, ...change } : rule));
  }
  function move(index: number, direction: -1 | 1) {
    const reordered = [...rules];
    const [rule] = reordered.splice(index, 1);
    reordered.splice(index + direction, 0, rule);
    setRules(reordered);
  }

  return <section className="settings-card" aria-labelledby="link-routing-heading">
    <h2 id="link-routing-heading">{t('settings.routes.title')}</h2>
    <p>{t('settings.routes.detail')}</p>
    <form onSubmit={event => {
      event.preventDefault();
      if (!disabled && dirty && valid) void save(draft);
    }}>
      <ol className="settings-link-routes">
        {rules.map(({ id, match, pattern, destination }, index) => <li key={id}>
          <fieldset className="settings-link-route" disabled={disabled}>
            <legend>{t('settings.routes.rule', { number: index + 1 })}</legend>
            <div className="settings-link-route-fields">
              <label><span>{t('settings.routes.match')}</span><select value={match}
                onChange={event => update(id, { match: event.target.value as LinkRoute['match'] })}>
                <option value="contains">{t('settings.routes.contains')}</option>
                <option value="exact">{t('settings.routes.exact')}</option>
                <option value="regex">{t('settings.routes.regex')}</option>
              </select></label>
              <label><span>{t('settings.routes.address')}</span><input value={pattern} required maxLength={maxLinkPatternBytes}
                ref={added === id ? focusOnMount : undefined} spellCheck={false} autoCapitalize="none" autoCorrect="off"
                placeholder={linkRoutePlaceholders[match]}
                onChange={event => update(id, { pattern: event.target.value })} /></label>
              <label><span>{t('settings.routes.destination')}</span><select value={destination}
                onChange={event => update(id, { destination: event.target.value })}><LinkDestinationOptions selected={destination} spaces={spaces} /></select></label>
            </div>
            <div className="settings-link-route-actions">
              <button type="button" className="site-search-icon-button" disabled={index === 0}
                aria-label={t('settings.routes.up')} title={t('settings.routes.up')} onClick={() => move(index, -1)}><ArrowUp size={16} /></button>
              <button type="button" className="site-search-icon-button" disabled={index === rules.length - 1}
                aria-label={t('settings.routes.down')} title={t('settings.routes.down')} onClick={() => move(index, 1)}><ArrowDown size={16} /></button>
              <button type="button" className="site-search-icon-button" aria-label={t('settings.routes.delete')}
                title={t('settings.routes.delete')} onClick={() => setRules(current => current.filter(rule => rule.id !== id))}><Trash2 size={16} /></button>
            </div>
          </fieldset>
        </li>)}
      </ol>
      <button type="button" className="settings-secondary settings-add-link-route" disabled={disabled || rules.length >= maxLinkRoutes}
        onClick={() => {
          const id = nextId.current++;
          setRules(current => [...current, { id, match: 'contains', pattern: '', destination: 'recent' }]);
          setAdded(id);
        }}><Plus size={16} />{t('settings.routes.add')}</button>
      <label className="settings-field"><span>{t('settings.routes.fallback')}</span><select value={fallback} disabled={disabled}
        onChange={event => setFallback(event.target.value)}><LinkDestinationOptions selected={fallback} spaces={spaces} /></select></label>
      {dirty && !valid && <p className="settings-note">{t('settings.routes.invalid')}</p>}
      <div className="settings-card-actions"><button type="submit" className="settings-secondary"
        disabled={disabled || !dirty || !valid}>{t('common.save')}</button></div>
    </form>
  </section>;
}
