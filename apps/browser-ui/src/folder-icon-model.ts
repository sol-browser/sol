/** A folder's glyph (docs/folder-icons.md). The id is stored in the workspace and synced,
 *  so ids never change meaning; native checks their shape (tree_store.h ValidFolderIcon).
 *  Names are English like the emoji picker's; search also takes Russian and Ukrainian. */
export type FolderIconEntry = { id: string; name: string; keywords: string };

export const FOLDER_ICONS: readonly FolderIconEntry[] = [
  { id: 'briefcase', name: 'Work', keywords: 'job office business работа офис бизнес робота' },
  { id: 'house', name: 'Home', keywords: 'house personal дом личное дім особисте' },
  { id: 'star', name: 'Star', keywords: 'favorite important звезда избранное важное зірка' },
  { id: 'heart', name: 'Heart', keywords: 'love like сердце любовь серце' },
  { id: 'bookmark', name: 'Bookmark', keywords: 'saved read later закладка сохранённое прочитать позже' },
  { id: 'flag', name: 'Flag', keywords: 'goal mark флаг цель прапор мета' },
  { id: 'bell', name: 'Notifications', keywords: 'bell alert уведомления колокольчик сповіщення дзвоник' },
  { id: 'calendar', name: 'Calendar', keywords: 'date schedule plan календарь расписание план календар розклад' },
  { id: 'mail', name: 'Mail', keywords: 'email inbox почта письма пошта листи' },
  { id: 'message-circle', name: 'Chat', keywords: 'messages talk чат сообщения повідомлення' },
  { id: 'users', name: 'Team', keywords: 'people group friends команда люди друзья група друзі' },
  { id: 'user', name: 'Person', keywords: 'profile me человек профиль я людина профіль' },
  { id: 'code', name: 'Code', keywords: 'dev programming код разработка программирование розробка' },
  { id: 'square-terminal', name: 'Terminal', keywords: 'console shell cli терминал консоль термінал' },
  { id: 'bug', name: 'Bug', keywords: 'issue debug баг ошибка отладка помилка' },
  { id: 'database', name: 'Data', keywords: 'database storage данные база дані' },
  { id: 'cloud', name: 'Cloud', keywords: 'drive storage облако хмара' },
  { id: 'server', name: 'Server', keywords: 'hosting infra сервер хостинг' },
  { id: 'cpu', name: 'Hardware', keywords: 'chip computer tech железо компьютер техника залізо' },
  { id: 'globe', name: 'Web', keywords: 'internet world sites интернет мир сайты світ сайти' },
  { id: 'link', name: 'Links', keywords: 'url ссылки посилання' },
  { id: 'book-open', name: 'Reading', keywords: 'book docs library книга чтение документация читання' },
  { id: 'graduation-cap', name: 'Study', keywords: 'school university learn учёба школа университет навчання університет' },
  { id: 'pencil', name: 'Writing', keywords: 'edit notes draft письмо заметки черновик писати нотатки' },
  { id: 'file-text', name: 'Documents', keywords: 'docs files paper документы файлы документи' },
  { id: 'clipboard-list', name: 'Tasks', keywords: 'checklist todo задачи список завдання' },
  { id: 'list-todo', name: 'To-do', keywords: 'tasks checklist дела задачи справи' },
  { id: 'newspaper', name: 'News', keywords: 'articles blog новости статьи новини статті' },
  { id: 'inbox', name: 'Inbox', keywords: 'incoming входящие вхідні' },
  { id: 'archive', name: 'Archive', keywords: 'old storage box архив старое архів' },
  { id: 'music', name: 'Music', keywords: 'songs audio музыка песни музика пісні' },
  { id: 'headphones', name: 'Listening', keywords: 'podcast audio наушники подкаст навушники' },
  { id: 'mic', name: 'Podcast', keywords: 'microphone voice микрофон голос мікрофон' },
  { id: 'film', name: 'Movies', keywords: 'video cinema фильмы кино видео фільми відео' },
  { id: 'tv', name: 'TV', keywords: 'shows series streaming телевизор сериалы серіали' },
  { id: 'camera', name: 'Photo', keywords: 'pictures camera фото камера фотографии' },
  { id: 'image', name: 'Images', keywords: 'pictures gallery art картинки изображения галерея зображення' },
  { id: 'palette', name: 'Design', keywords: 'art colors creative дизайн искусство цвета мистецтво кольори' },
  { id: 'gamepad-2', name: 'Games', keywords: 'gaming play игры геймпад ігри' },
  { id: 'trophy', name: 'Achievements', keywords: 'sport win prize награды спорт победа нагороди перемога' },
  { id: 'shopping-cart', name: 'Shopping', keywords: 'store buy cart покупки магазин корзина кошик' },
  { id: 'shopping-bag', name: 'Bag', keywords: 'shop orders покупки заказы пакет замовлення' },
  { id: 'gift', name: 'Gifts', keywords: 'present wishlist подарки желания подарунки бажання' },
  { id: 'tag', name: 'Tag', keywords: 'label price sale метка тег цена мітка ціна' },
  { id: 'wallet', name: 'Finance', keywords: 'money budget wallet финансы деньги бюджет кошелёк гроші гаманець' },
  { id: 'credit-card', name: 'Payments', keywords: 'bank card платежи банк карта платежі' },
  { id: 'circle-dollar-sign', name: 'Money', keywords: 'dollar income деньги доход гроші дохід' },
  { id: 'chart-line', name: 'Analytics', keywords: 'stats chart growth аналитика статистика график аналітика графік' },
  { id: 'plane', name: 'Travel', keywords: 'trip flight vacation путешествия самолёт отпуск подорожі літак відпустка' },
  { id: 'map', name: 'Map', keywords: 'places location карта места місця' },
  { id: 'compass', name: 'Explore', keywords: 'discover navigation компас исследовать досліджувати' },
  { id: 'car', name: 'Car', keywords: 'auto transport машина авто транспорт' },
  { id: 'bike', name: 'Bike', keywords: 'cycling sport велосипед спорт' },
  { id: 'building-2', name: 'Company', keywords: 'office building city компания здание город компанія будівля місто' },
  { id: 'utensils', name: 'Food', keywords: 'recipes restaurant cooking еда рецепты ресторан кухня їжа рецепти' },
  { id: 'coffee', name: 'Coffee', keywords: 'cafe break кофе кафе перерыв кава перерва' },
  { id: 'dumbbell', name: 'Fitness', keywords: 'gym workout sport спорт зал тренировки тренування' },
  { id: 'heart-pulse', name: 'Health', keywords: 'medical doctor здоровье медицина врач здоров’я лікар' },
  { id: 'leaf', name: 'Nature', keywords: 'eco plants природа растения экология рослини' },
  { id: 'sprout', name: 'Growth', keywords: 'garden plant start рост сад начало ріст початок' },
  { id: 'paw-print', name: 'Pets', keywords: 'animals dog cat питомцы животные собака кошка тварини кіт' },
  { id: 'baby', name: 'Kids', keywords: 'children family дети семья діти родина' },
  { id: 'sun', name: 'Day', keywords: 'weather summer солнце день лето сонце літо' },
  { id: 'moon', name: 'Night', keywords: 'sleep evening луна ночь вечер місяць ніч' },
  { id: 'zap', name: 'Quick', keywords: 'fast energy lightning быстро энергия молния швидко блискавка' },
  { id: 'flame', name: 'Hot', keywords: 'fire trending огонь горячее вогонь гаряче' },
  { id: 'sparkles', name: 'Ideas', keywords: 'ai magic new идеи магия новое ідеї нове' },
  { id: 'lightbulb', name: 'Inspiration', keywords: 'idea think вдохновение идея лампочка натхнення' },
  { id: 'rocket', name: 'Launch', keywords: 'startup project ship запуск стартап проект проєкт' },
  { id: 'target', name: 'Goals', keywords: 'focus aim цели фокус цілі' },
  { id: 'puzzle', name: 'Extensions', keywords: 'plugins puzzle расширения пазл розширення' },
  { id: 'wrench', name: 'Tools', keywords: 'fix utilities инструменты утилиты інструменти' },
  { id: 'hammer', name: 'Build', keywords: 'diy make строить сделать будувати' },
  { id: 'settings', name: 'Settings', keywords: 'config admin настройки параметры налаштування' },
  { id: 'lock', name: 'Private', keywords: 'secure secret приватное замок секрет приватне' },
  { id: 'shield', name: 'Security', keywords: 'safety protect безопасность защита безпека захист' },
  { id: 'key-round', name: 'Accounts', keywords: 'passwords login аккаунты пароли вход акаунти паролі' },
];

const byId = new Map(FOLDER_ICONS.map(entry => [entry.id, entry]));
/** The catalog entry of a stored id; undefined for "" and for ids this build does not know. */
export const folderIconEntry = (id: string | undefined) => (id ? byId.get(id) : undefined);

/** Entries matching every word of |query| in their id, name or keywords; all of them for "". */
export function findFolderIcons(query: string): FolderIconEntry[] {
  const words = query.toLocaleLowerCase().replaceAll('ё', 'е').split(/\s+/u).filter(Boolean);
  if (!words.length) return [...FOLDER_ICONS];
  return FOLDER_ICONS.filter(entry => {
    const text = `${entry.id} ${entry.name} ${entry.keywords}`.toLocaleLowerCase().replaceAll('ё', 'е');
    return words.every(word => text.includes(word));
  });
}
