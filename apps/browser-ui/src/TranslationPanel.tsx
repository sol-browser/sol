import { useState } from 'react';
import { useIntl } from 'react-intl';
import type { PageTranslation, SelectionTranslation } from '../../../packages/browser-contracts/src';
import { useMessages, type Translate } from './i18n';
import { Icon } from './Icon';

export function translationLanguageName(names: Intl.DisplayNames, code: string) {
  return names.of(code.replace('_', '-')) ?? code;
}

type Props = { page: PageTranslation; languages: string[]; privateWindow: boolean; command: (request: object) => void };

function pageTitle(page: PageTranslation, names: Intl.DisplayNames, t: Translate) {
  switch (page.phase) {
    case 'error': return t('translation.error');
    case 'working': return t('translation.working', { language: translationLanguageName(names, page.to) });
    case 'translated':
      if (page.from) return t('translation.translated', { language: translationLanguageName(names, page.from) });
      break;
    default: break;
  }
  return t('translation.question', { language: translationLanguageName(names, page.to) });
}

function selectionStatus(result: SelectionTranslation, names: Intl.DisplayNames, t: Translate) {
  switch (result.phase) {
    case 'working': return t('translation.working', { language: translationLanguageName(names, result.to) });
    case 'same': return t('translation.selectionSame', { language: translationLanguageName(names, result.to) });
    case 'unavailable': return t('translation.selectionUnavailable');
    default: return t('translation.selectionError');
  }
}

function ModelProgress({ percent }: { percent: number }) {
  const t = useMessages();
  return <div className="translation-model-progress" role="progressbar" aria-label={t('translation.downloading')}
    aria-valuenow={percent} aria-valuemin={0} aria-valuemax={100}>
    <div><span>{t('translation.downloading')}</span><span>{percent}%</span></div>
    <span className="download-progress"><span style={{ width: `${percent}%` }} /></span>
  </div>;
}

export function TranslationPanel({ page, languages, privateWindow, command }: Props) {
  const t = useMessages();
  const { locale } = useIntl();
  const names = new Intl.DisplayNames(locale, { type: 'language' });
  const [view, setView] = useState<'main' | 'options' | 'languages'>('main');
  const translating = page.phase === 'working';
  const translated = page.phase === 'translated';
  function translate(language?: string) {
    setView('main');
    command({ method: 'translation.accept', id: page.id, ...(language ? { target: language } : {}) });
  }
  const choices = languages.map(code => ({ code, name: translationLanguageName(names, code) }))
    .sort((a, b) => a.name.localeCompare(b.name, locale));
  return <section className="popover translation-popover" aria-label={t('translation.title')}>
    {view === 'main' ? <>
      <header><h2><span role="status">{pageTitle(page, names, t)}</span></h2></header>
      {page.download !== undefined && <ModelProgress percent={page.download} />}
      <div className="translation-actions">
        <button type="button" className="popover-action-button" disabled={translating} data-action={translated ? 'translation-original' : 'translation-start'}
          onClick={() => translated ? command({ method: 'translation.original', id: page.id }) : translate()}>
          {translated ? t('translation.original') : t('translation.start')}
        </button>
        <button type="button" className="popover-action-button" disabled={translating} data-action="translation-options" onClick={() => setView('options')}>{t('translation.options')}</button>
      </div>
    </> : <>
      <header>
        <button type="button" className="popover-back" aria-label={t('translation.back')} onClick={() => setView(view === 'languages' ? 'options' : 'main')}><Icon name="chevron" /></button>
        <h2>{t(view === 'languages' ? 'translation.choose' : 'translation.options')}</h2>
      </header>
      {view === 'options' ? <div className="translation-options">
        <button type="button" data-action="translation-choose" disabled={!choices.length} onClick={() => setView('languages')}>{t('translation.choose')}<Icon name="chevron" /></button>
        {!privateWindow && <button type="button" data-action="translation-never" onClick={() => command({ method: 'translation.never', id: page.id })}>{t('translation.never')}</button>}
        <button type="button" data-action="translation-settings" onClick={() => command({ method: 'translation.settings', id: page.id })}>{t('translation.settings')}</button>
      </div> : <ul className="translation-languages">
        {choices.map(({ code, name }) => <li key={code}><button type="button" data-language={code}
          disabled={translated && page.to === code} onClick={() => translate(code)}>{name}{page.to === code && <Icon name="check" />}</button></li>)}
      </ul>}
    </>}
  </section>;
}

export function SelectionTranslationPanel({ result, command }: { result: SelectionTranslation; command: (request: object) => Promise<void> }) {
  const t = useMessages();
  const { locale } = useIntl();
  const [copy, setCopy] = useState<'idle' | 'copied' | 'error'>('idle');
  const names = new Intl.DisplayNames(locale, { type: 'language' });
  let content;
  if (result.phase === 'translated') {
    content = <>
      <p className="selection-translation-text" dir="auto" data-testid="translated-selection">{result.text}</p>
      <footer className="popover-footer">
        <button type="button" className="popover-text-button" onClick={() => void command({ method: 'translation.copySelection' })
          .then(() => setCopy('copied'), () => setCopy('error'))}>{t(copy === 'copied' ? 'translation.selectionCopied' : 'translation.selectionCopy')}</button>
      </footer>
      {copy === 'error' && <p className="popover-error" role="alert">{t('translation.selectionCopyError')}</p>}
    </>;
  } else if (result.download !== undefined) {
    content = <ModelProgress percent={result.download} />;
  } else {
    content = <p role="status">{selectionStatus(result, names, t)}</p>;
  }
  return <section className="popover translation-popover selection-translation" aria-labelledby="selection-translation-title">
    <header>
      <h2 id="selection-translation-title">{t('translation.selectionTitle')}</h2>
      <button type="button" className="popover-text-button" aria-label={t('translation.close')}
        onClick={() => void command({ method: 'panel.close' }).catch(() => undefined)}><Icon name="close" /></button>
    </header>
    {content}
  </section>;
}
