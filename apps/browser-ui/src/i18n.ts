import { useCallback } from 'react';
import { createIntl, useIntl, type IntlShape } from 'react-intl';
import english from './locales/en.json' with { type: 'json' };
import ukrainian from './locales/uk.json' with { type: 'json' };

export type MessageId = keyof typeof english;
export type Translate = (id: MessageId, values?: Record<string, string | number>) => string;
type Catalog = Partial<Record<MessageId, { defaultMessage: string }>>;
const catalogs: Record<string, Catalog> = { en: english, uk: ukrainian };

export function createInterfaceIntl(language: string): IntlShape {
  const locale = language === 'uk' ? 'uk' : 'en';
  const messages = Object.fromEntries(Object.entries(catalogs[locale]).map(
    ([id, entry]) => [id, entry.defaultMessage]));
  return createIntl({ locale, defaultLocale: 'en', messages });
}

export function message(intl: IntlShape, id: MessageId, values?: Record<string, string | number>): string {
  return intl.formatMessage(messageDescriptor(id), values);
}

export function messageDescriptor(id: MessageId) {
  return { id, defaultMessage: english[id].defaultMessage };
}

export function useMessages(): Translate {
  const intl = useIntl();
  return useCallback((id, values) => message(intl, id, values), [intl]);
}
