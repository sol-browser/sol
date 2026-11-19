import { useEffect, useState } from 'react';
import { ArrowDown, ArrowUp, CornerDownRight, Link2, Plus, Route, Trash2 } from 'lucide-react';
import { maxLinkPatternBytes, maxLinkRoutes, parseLinkRoutingSnapshot, validLinkRouting, type LinkRoute, type LinkRouting,
  type LinkRoutingSnapshot, type SpaceInfo } from '../../../packages/browser-contracts/src';
import { Icon } from './Icon';
import { useMessages } from './i18n';
import { linkRoutePlaceholders, trimSpaces } from './LinkRoutingSettings';
import { LibrarySelect, type LibraryChoice } from './LibrarySelect';
import { spaceDisplayName } from './sidebar-model';
import { focusOnMount } from './focus-on-mount';

type RoutesCommand = { method: 'library.getRoutes' } | { method: 'library.saveRoutes'; routing: LinkRouting; expectedRevision: number };
type EditingRule = { index: number | null; rule: LinkRoute };
const matchTypes = ['contains', 'exact', 'regex'] as const;

function routeRequest(command: RoutesCommand) {
  return new Promise<LinkRoutingSnapshot>((resolve, reject) => {
    if (!window.cefQuery) { reject(new Error('Native bridge unavailable')); return; }
    let query = -1;
    const timeout = window.setTimeout(() => {
      if (query >= 0) window.cefQueryCancel?.(query);
      reject(new Error('Native request timed out'));
    }, 5000);
    query = window.cefQuery({ request: JSON.stringify(command), persistent: false, onSuccess: raw => {
      clearTimeout(timeout);
      try { resolve(parseLinkRoutingSnapshot(JSON.parse(raw))); } catch (error) { reject(error); }
    }, onFailure: (code, message) => { clearTimeout(timeout); reject(new Error(`${code}: ${message}`)); } });
  });
}

export function LibraryRoutes({ spaces }: { spaces: SpaceInfo[] }) {
  const t = useMessages();
  const [settings, setSettings] = useState<LinkRoutingSnapshot>();
  // Little Sol, the most recent Space, every Space; a deleted Space stays named as missing.
  function destinations(selected: string): LibraryChoice[] {
    const choices: LibraryChoice[] = [{ value: 'littleSol', label: 'Little Sol' }, { value: 'recent', label: t('settings.routes.recent') },
      ...[...spaces].sort((a, b) => a.position - b.position).map(space => ({ value: space.id, label: spaceDisplayName(space, t), group: t('sidebar.spaces') }))];
    if (!choices.some(choice => choice.value === selected)) choices.push({ value: selected, label: t('settings.routes.missing'), disabled: true });
    return choices;
  }
  const [editing, setEditing] = useState<EditingRule>();
  const [search, setSearch] = useState('');
  const [error, setError] = useState('');
  const [saving, setSaving] = useState(false);
  const [load, setLoad] = useState(0);
  useEffect(() => {
    let current = true;
    void routeRequest({ method: 'library.getRoutes' }).then(value => {
      if (current) { setSettings(value); setEditing(undefined); setError(''); }
    }).catch(() => { if (current) setError(t('library.routesFailed')); });
    return () => { current = false; };
  }, [load, t]);

  async function save(routing: LinkRouting) {
    if (!settings || saving || !validLinkRouting(routing)) return false;
    setSaving(true);
    setError('');
    try {
      setSettings(await routeRequest({ method: 'library.saveRoutes', expectedRevision: settings.revision, routing }));
      return true;
    } catch (failure) {
      setError(failure instanceof Error ? failure.message : String(failure));
      return false;
    } finally { setSaving(false); }
  }

  const routing = settings?.routing;
  const searchText = search.trim().toLocaleLowerCase();
  const visibleRules = (routing?.rules ?? []).map((rule, index) => {
    const space = spaces.find(space => space.id === rule.destination);
    const destination = space ? spaceDisplayName(space, t) : rule.destination === 'littleSol' ? 'Little Sol' :
      t(rule.destination === 'recent' ? 'settings.routes.recent' : 'settings.routes.missing');
    return { rule, index, destination };
  }).filter(({ rule, destination }) => `${rule.pattern} ${destination} ${t(`library.match.${rule.match}`)}`.toLocaleLowerCase().includes(searchText));

  function updateRule(patch: Partial<LinkRoute>) {
    setEditing(current => current && { ...current, rule: { ...current.rule, ...patch } });
  }

  async function moveRule(index: number, offset: number) {
    if (!routing) return;
    const destination = index + offset;
    const rules = [...routing.rules];
    if (destination < 0 || destination >= rules.length) return;
    [rules[index], rules[destination]] = [rules[destination], rules[index]];
    if (await save({ ...routing, rules })) setEditing(undefined);
  }

  const draft = editing && { ...editing.rule, pattern: trimSpaces(editing.rule.pattern) };
  const draftRules = [...(routing?.rules ?? [])];
  if (draft && editing) draftRules[editing.index ?? draftRules.length] = draft;
  const valid = !!routing && validLinkRouting({ ...routing, rules: draftRules });
  const heading = editing ? editing.index === null ? 'library.newRoute' : 'library.editRoute' : 'settings.routes.title';
  return <section className="library-routes" aria-labelledby="library-routes-title">
    <h1 id="library-routes-title" className={editing ? 'library-editor-title' : 'sr-only'}>{t(heading)}</h1>
    {error && <div role="alert" className="library-notice">{error}{' '}
      <button type="button" className="library-action" disabled={saving} onClick={() => setLoad(value => value + 1)}>
        {t('library.reloadRoutes')}
      </button>
    </div>}
    {!routing && !error && <p className="library-notice" role="status">{t('toolbar.loading')}</p>}
    {routing && (editing ? <form className="library-route-editor library-scroll" onSubmit={async event => {
      event.preventDefault();
      if (await save({ ...routing, rules: draftRules })) setEditing(undefined);
    }}>
      <fieldset disabled={saving} className="library-route-fields">
        <legend className="library-field-label">{t('library.whenLink')}</legend>
        <div className="library-match-types" role="group" aria-label={t('settings.routes.match')}>
          {matchTypes.map(match => <button key={match} type="button" aria-pressed={editing.rule.match === match}
            onClick={() => updateRule({ match })}>{t(`library.match.${match}`)}</button>)}
        </div>
        <input type="text" ref={focusOnMount} required maxLength={maxLinkPatternBytes} spellCheck={false} autoCapitalize="none" autoCorrect="off"
          aria-label={t('settings.routes.address')} placeholder={linkRoutePlaceholders[editing.rule.match]} value={editing.rule.pattern}
          onChange={event => updateRule({ pattern: event.target.value })} />
        <label><span className="library-field-label">{t('library.openIn')}</span>
          <LibrarySelect label={t('library.openIn')} value={editing.rule.destination} choices={destinations(editing.rule.destination)}
            onChange={destination => updateRule({ destination })} />
        </label>
      </fieldset>
      <footer className="library-route-actions">
        {editing.index !== null && <button type="button" className="library-icon-button library-delete-route"
          data-tip={t('settings.routes.delete')} aria-label={t('settings.routes.delete')} disabled={saving}
          onClick={async () => {
            if (await save({ ...routing, rules: routing.rules.filter((_, index) => index !== editing.index) })) setEditing(undefined);
          }}><Trash2 size={16} aria-hidden="true" /></button>}
        <button type="button" className="library-action" disabled={saving} onClick={() => { setEditing(undefined); setError(''); }}>{t('common.cancel')}</button>
        <button type="submit" className="library-action library-primary" disabled={saving || !valid}>
          {t(editing.index === null ? 'library.addRoute' : 'common.save')}
        </button>
      </footer>
    </form> : <>
      <div className="library-toolbar">
        <label className="library-search"><Icon name="search" /><input type="search" aria-label={t('library.searchRoutes')}
          placeholder={t('library.searchRoutes')} value={search} onChange={event => setSearch(event.target.value)} /></label>
        <button className="library-action" type="button" data-action="new-library-route"
          disabled={saving || routing.rules.length >= maxLinkRoutes}
          onClick={() => setEditing({ index: null, rule: { match: 'contains', pattern: '', destination: 'recent' } })}>
          <Plus size={15} strokeWidth={1.9} aria-hidden="true" />{t('library.newRoute')}
        </button>
      </div>
      <div className="library-scroll library-route-scroll">
        {visibleRules.length ? <ol className="library-list">{visibleRules.map(({ rule, index, destination }) => <li className="library-route-row" key={index}>
          <button type="button" className="library-row" disabled={saving} onClick={() => setEditing({ index, rule })}>
            <Link2 size={16} strokeWidth={1.8} aria-hidden="true" /><span className="library-row-text">
              <strong data-tip={rule.pattern}><span className="library-match-label">{t(`library.match.${rule.match}`)}</span>{rule.pattern}</strong>
              <small><CornerDownRight size={13} strokeWidth={1.7} aria-hidden="true" />{destination}</small>
            </span>
          </button>
          <div className="library-route-order">
            <button type="button" className="library-icon-button" aria-label={t('settings.routes.up')} data-tip={t('settings.routes.up')}
              disabled={saving || index === 0} onClick={() => void moveRule(index, -1)}><ArrowUp size={14} aria-hidden="true" /></button>
            <button type="button" className="library-icon-button" aria-label={t('settings.routes.down')} data-tip={t('settings.routes.down')}
              disabled={saving || index === routing.rules.length - 1} onClick={() => void moveRule(index, 1)}><ArrowDown size={14} aria-hidden="true" /></button>
          </div>
        </li>)}</ol> : <div className="library-empty"><Route size={20} aria-hidden="true" /><p>{t(searchText ? 'library.noMatchingRoutes' : 'library.noRoutes')}</p></div>}
      </div>
      <label className="library-route-fallback"><span className="library-field-label">{t('settings.routes.fallback')}</span>
        <LibrarySelect label={t('settings.routes.fallback')} value={routing.fallback} disabled={saving}
          choices={destinations(routing.fallback)} onChange={fallback => void save({ ...routing, fallback })} />
      </label>
    </>)}
  </section>;
}
