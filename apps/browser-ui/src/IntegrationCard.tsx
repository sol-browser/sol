import { useEffect, useLayoutEffect, useRef, useState, type CSSProperties } from 'react';
import type { IntegrationCard as Card } from './integrations';
import { Icon } from './Icon';

const serviceMarks = {
  notion: <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.7" strokeLinejoin="round">
    <rect x="4" y="4" width="16" height="16" rx="3" />
    <path d="M8.5 16V8l7 8V8" strokeLinecap="round" />
  </svg>,
  linear: <svg viewBox="0 0 24 24" fill="currentColor">
    <path d="M6.3 5.7A9 9 0 0 1 18.3 19zM3.6 9.7c.3-1 .8-1.8 1.4-2.6L16.8 20c-.9.4-1.9.8-3 .9zM3.1 12l8 8.7A9 9 0 0 1 3.1 12z" />
  </svg>,
  confluence: <svg viewBox="0 0 24 24" fill="none" strokeWidth="4" strokeLinecap="round">
    <path d="M5 7.5c4.3 0 7.3 5.5 11 5.5 1.6 0 2.7-.5 3.5-1.5" stroke="currentColor" />
    <path d="M19 16.5c-4.3 0-7.3-5.5-11-5.5-1.6 0-2.7.5-3.5 1.5" stroke="currentColor" opacity=".64" />
  </svg>,
};

const hourLabel = new Intl.DateTimeFormat(undefined, { hour: 'numeric', timeZone: 'UTC' });
const timeLabel = new Intl.DateTimeFormat(undefined, { hour: 'numeric', minute: '2-digit', timeZone: 'UTC' });
const hourHeight = 40;
const localMinute = (date: Date) => date.getHours() * 60 + date.getMinutes();
const relativeVisit = new Intl.RelativeTimeFormat(undefined, { numeric: 'auto' });

function VisitTime({ visitedAt, now }: { visitedAt: number; now: number }) {
  const minutes = Math.max(0, Math.floor((now - visitedAt) / 60000));
  const label = minutes === 0 ? relativeVisit.format(0, 'second') : minutes < 60 ? relativeVisit.format(-minutes, 'minute') :
    minutes < 1440 ? relativeVisit.format(-Math.floor(minutes / 60), 'hour') : relativeVisit.format(-Math.floor(minutes / 1440), 'day');
  const date = new Date(visitedAt);
  return <time dateTime={date.toISOString()} title={date.toLocaleString()}>{label}</time>;
}

function CalendarNow({ active }: { active: boolean }) {
  const [now, setNow] = useState(() => new Date());
  useEffect(() => {
    if (!active) return;
    let timer: number;
    const update = () => {
      const date = new Date();
      setNow(date);
      timer = window.setTimeout(update, 60000 - date.getTime() % 60000);
    };
    update();
    return () => window.clearTimeout(timer);
  }, [active]);
  const minute = localMinute(now);
  const label = timeLabel.format(minute * 60000);
  return <div className="integration-now" style={{ '--now-position': `${minute / 14.4}%` } as CSSProperties}>
    <time dateTime={now.toISOString()} aria-label={`Current local time ${label}`}>{label}</time>
  </div>;
}

function CalendarAgenda({ items, open, join, active }: { items: Card['items']; open: (index: number) => void;
  join: (index: number) => void; active: boolean }) {
  const entries = items.map((item, index) => ({ item, index }));
  const timed = entries.flatMap(entry => entry.item.startMinute === undefined ? [] : [{ ...entry, start: entry.item.startMinute }])
    .sort((a, b) => a.start - b.start);
  const untimed = entries.filter(({ item }) => item.startMinute === undefined);
  const viewport = useRef<HTMLDivElement>(null);
  useLayoutEffect(() => {
    if (active && viewport.current) viewport.current.scrollTop = (new Date().getHours() - 1) * hourHeight;
  }, [active]);

  // Cluster overlapping blocks and reuse a free lane inside each cluster.
  const groups: { end: number; lanes: number[]; events: (typeof timed[number] & { end: number; lane: number })[] }[] = [];
  for (const event of timed) {
    let group = groups.at(-1);
    if (!group || event.start >= group.end) {
      group = { end: 0, lanes: [], events: [] };
      groups.push(group);
    }
    const end = Math.min(1440, Math.max(event.start + 32, event.item.endMinute ?? 0));
    const free = group.lanes.findIndex(until => until <= event.start);
    const lane = free === -1 ? group.lanes.length : free;
    group.lanes[lane] = end;
    group.end = Math.max(group.end, end);
    group.events.push({ ...event, end, lane });
  }
  return <>
    {untimed.length > 0 && <>
      <p className="integration-untimed-label">Other events</p>
      <ul className="integration-untimed">{untimed.map(({ item, index }) => <li className="integration-item" key={item.token}>
        <button type="button" className="archive-row" title={item.title} onClick={() => open(index)}>
          <span className="archive-text"><span>{item.title}</span></span>
        </button>
        {item.join && <button type="button" className="popover-text-button integration-join" onClick={() => join(index)}>Join meeting</button>}
      </li>)}</ul>
    </>}
    <div className="integration-agenda" ref={viewport} aria-label="Hourly agenda" tabIndex={0}>
      <div className="integration-day" style={{ '--hour-height': `${hourHeight}px` } as CSSProperties}>
        <ol className="integration-hours" aria-hidden="true">
          {Array.from({ length: 24 }, (_, hour) => <li key={hour} className="integration-hour">{hourLabel.format(hour * 3600000)}</li>)}
        </ol>
        <div className="integration-events">
          {groups.flatMap(group => group.events.map(({ item, index, start, end, lane }) => {
            const time = `${timeLabel.format(start * 60000)}${item.endMinute === undefined ? '' : ` – ${timeLabel.format(item.endMinute * 60000)}`}`;
            return <div key={item.token} className="integration-event" data-compact={end - start < 48}
              style={{ top: `${start / 14.4}%`, height: `${(end - start) / 14.4}%`,
                left: `${lane * 100 / group.lanes.length}%`, width: `${100 / group.lanes.length}%` }}>
              <button type="button" className="integration-event-open" title={`${item.title} · ${time}`} onClick={() => open(index)}>
                <span>{item.title}</span><small>{time}</small>
              </button>
              {item.join && <button type="button" className="popover-text-button integration-join" aria-label="Join meeting" title="Join meeting" onClick={() => join(index)}>
                <Icon name="camera" />
              </button>}
            </div>;
          }))}
        </div>
        <CalendarNow active={active} />
      </div>
    </div>
  </>;
}

export function IntegrationCard({ card, open, join, control, active }: { card: Card; active: boolean; open: (index: number) => void;
  join: (index: number) => void; control: (action: 'open' | 'refresh' | 'compose' | 'createFigma') => void }) {
  const mail=card.kind==='mail', calendar=card.kind==='calendar', figma=card.service==='figma';
  const recentMail=card.mailView==='recent'||card.mailView==='recentAll';
  const mark = serviceMarks[card.service as keyof typeof serviceMarks];
  const hasAddAction = figma || (mail && card.composeAvailable === true);
  const addLabel = figma ? 'New Figma Design' : 'Compose email';
  const now = figma ? Date.now() : 0;
  return <section className="popover integration-popover" data-kind={card.kind} data-service={card.service}
    data-add-action={hasAddAction} aria-label={`${card.name} preview`}>
    {!mail && !calendar && !figma && <header>
      <span className="integration-mark" aria-hidden="true">{mark}</span>
      <div className="integration-identity"><h2>{card.name}</h2><span>Recent pages</span></div>
    </header>}
    {hasAddAction &&
      <button type="button" className="popover-text-button integration-add" aria-label={addLabel} title={addLabel} onClick={()=>control(figma?'createFigma':'compose')}><Icon name="plus" /></button>
    }
    {card.loading ? <p className="popover-empty" role="status">{mail?'Preparing inbox preview…':'Loading preview…'}</p> : card.status==='signInRequired' ? <>
      <p className="popover-empty">Outlook needs you to sign in.</p>
      <button type="button" className="popover-text-button" onClick={()=>control('open')}>Sign in to Outlook</button>
    </> : card.status==='openTab' ? <>
      <p className="popover-empty">Open this pinned tab to load its preview.</p>{!mail && <button className="popover-text-button" onClick={()=>control('open')}>Open {card.name}</button>}
    </> : card.status==='unsupported' ? <>
      <p className="popover-empty">{mail?'Inbox preview unavailable.':'This calendar view is not recognized yet.'}</p>
      {!mail && <button className="popover-text-button" onClick={()=>control('open')}>Open {card.name}</button>}
    </> : calendar ? <CalendarAgenda items={card.items} open={open} join={join} active={active} /> : card.items.length && mail ?
      <ul className="integration-mail-feed">{card.items.map((item, index) => <li className="integration-item" key={item.token}>
        <button type="button" className="archive-row integration-message" title={item.title} onClick={() => open(index)}>
          <span className="integration-message-heading">
            <strong>{item.detail || item.title}</strong>
            {item.displayTime && <span className="integration-message-time" title={item.displayTime}>{item.displayTime}</span>}
          </span>
          {item.detail && <span className="integration-message-subject">{item.title}</span>}
        </button>
      </li>)}</ul> : card.items.length && figma ?
      <ul className="integration-figma-feed">{card.items.map((item, index) => <li className="integration-item" key={item.url}>
        <button type="button" className="archive-row integration-figma-row" title={item.title || item.url} onClick={() => open(index)}>
          <span className="archive-text"><span>{item.title || item.url}</span>
            {item.visitedAt !== undefined && <VisitTime visitedAt={item.visitedAt} now={now} />}</span>
        </button>
      </li>)}</ul> : card.items.length ?
      <ul>{card.items.map((item, index) => <li className="integration-item" key={item.url??item.token}><button className="archive-row" title={item.title || item.url} onClick={() => open(index)}>
        <Icon name="history" /><span className="archive-text"><span>{item.title || item.url}</span>
          {(item.detail||item.url) && <span>{item.detail || (item.url?new URL(item.url).hostname:'')}</span>}</span><Icon name="arrow" className="archive-go" />
      </button></li>)}</ul> :
      <p className="popover-empty">{mail?(recentMail?'No messages in this inbox view.':'No unread messages in this inbox view.'):'Pages you visit in this profile will appear here.'}</p>}
    {calendar && <footer className="integration-controls">
      <button type="button" className="popover-text-button" aria-label="Refresh preview" title="Refresh preview" onClick={()=>control('refresh')}><Icon name="reload" /></button>
      <button type="button" className="popover-text-button" aria-label="Open calendar" title="Open calendar" onClick={()=>control('open')}><Icon name="arrow" /></button>
    </footer>}
  </section>;
}
