#pragma once
#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace red {
struct IntegrationSetting { std::string_view id, name; bool available = true; };
inline constexpr IntegrationSetting kIntegrationSettings[] = {
    {"gmail","Gmail"}, {"outlookMail","Outlook Mail"}, {"googleCalendar","Google Calendar"}, {"figma","Figma"},
    {"notion","Notion",false}, {"linear","Linear",false}, {"confluence","Confluence",false},
    {"whatsapp","WhatsApp"}, {"twitter","X"}, {"spotify","Spotify"}, {"youtube","YouTube"}, {"googleMeet","Google Meet"},
    {"googleDocs","Google Docs"}, {"word","Word"}};
using IntegrationSettings = std::map<std::string, bool, std::less<>>;
// The stored catalog includes paused providers so old preferences can be read.
inline const IntegrationSetting* FindStoredIntegrationSetting(std::string_view id) {
  const auto found = std::ranges::find(kIntegrationSettings, id, &IntegrationSetting::id);
  return found == std::end(kIntegrationSettings) ? nullptr : found;
}
inline const IntegrationSetting* FindIntegrationSetting(std::string_view id) {
  const auto* provider = FindStoredIntegrationSetting(id);
  return provider && provider->available ? provider : nullptr;
}
inline bool IntegrationEnabled(const IntegrationSettings& settings, std::string_view id) {
  if (!FindIntegrationSetting(id)) return false;
  const auto value = settings.find(id);
  return value == settings.end() || value->second;
}
struct RecentProvider { std::string id, name, host, query; };
struct IntegrationAddress { std::string host, path; };
struct LiveProvider {
  std::string id, name, base;
  bool IsMail() const { return id == "gmail" || id == "outlookMail"; }
};
// Deliberately strict: integrations only read HTTPS sites on their normal port.
inline std::optional<IntegrationAddress> IntegrationUrl(std::string_view url) {
  if (url.size() > 4096 || !url.starts_with("https://")) return std::nullopt;
  if (std::any_of(url.begin(), url.end(), [](unsigned char c) { return c <= 32 || c == 127 || c == '\\'; })) return std::nullopt;
  const auto end = url.find_first_of("/?#", 8);
  std::string host(url.substr(8, end == url.npos ? url.size() - 8 : end - 8));
  if (host.ends_with(":443")) host.resize(host.size() - 4);
  if (host.empty() || std::any_of(host.begin(), host.end(), [](unsigned char c) {
    return !(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') && c != '.' && c != '-';
  })) return std::nullopt;
  std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); });
  std::string path = "/";
  if (end != url.npos && url[end] == '/') { const auto last=url.find_first_of("?#",end); path=std::string(url.substr(end,last==url.npos?url.npos:last-end)); }
  return IntegrationAddress{host, path};
}
inline std::optional<RecentProvider> RecentIntegration(std::string_view url) {
  const auto address = IntegrationUrl(url); if (!address) return std::nullopt;
  const auto& host = address->host;
  if (host == "www.figma.com" || host == "figma.com") return RecentProvider{"figma", "Figma", host, "figma.com/"};
  if (host == "linear.app" && FindIntegrationSetting("linear")) return RecentProvider{"linear", "Linear", host, "linear.app/"};
  if ((host == "www.notion.so" || host == "notion.so" || host.ends_with(".notion.site")) && FindIntegrationSetting("notion"))
    return RecentProvider{"notion", "Notion", host, host + "/"};
  if (host.ends_with(".atlassian.net") && (address->path == "/wiki" || address->path.starts_with("/wiki/")) && FindIntegrationSetting("confluence"))
    return RecentProvider{"confluence", "Confluence", host, host + "/wiki"};
  return std::nullopt;
}
inline std::optional<LiveProvider> GoogleIntegration(std::string_view url) {
  const auto address = IntegrationUrl(url); if (!address) return std::nullopt;
  const bool mail = address->host == "mail.google.com", calendar = address->host == "calendar.google.com";
  if (!mail && !calendar) return std::nullopt;
  const std::string prefix = mail ? "/mail/u/" : "/calendar/u/";
  if (!address->path.starts_with(prefix)) return std::nullopt;
  const auto end = address->path.find('/', prefix.size());
  if (end == address->path.npos || end == prefix.size() || end - prefix.size() > 3 ||
      !std::all_of(address->path.begin() + prefix.size(), address->path.begin() + end, [](unsigned char c) { return c >= '0' && c <= '9'; })) return std::nullopt;
  return LiveProvider{mail ? "gmail" : "googleCalendar", mail ? "Gmail" : "Google Calendar", "https://" + address->host + address->path.substr(0, end + 1)};
}
inline std::optional<LiveProvider> LiveIntegration(std::string_view url) {
  if (auto google=GoogleIntegration(url)) return google;
  const auto a=IntegrationUrl(url);
  if (a && a->host=="outlook.live.com" && (a->path=="/mail" || a->path.starts_with("/mail/")))
    return LiveProvider{"outlookMail","Outlook Mail","https://outlook.live.com/mail/"};
  return std::nullopt;
}
// Canonical mailbox identity comes from the saved pin, never the current tab.
// The selected account slot is preserved; an unscoped URL uses the site's
// default slot. Queries, folders, searches, messages and hashes are dropped.
inline std::optional<std::string> MailboxTarget(std::string_view url) {
  const auto a = IntegrationUrl(url);
  if (!a) return std::nullopt;
  const bool gmail = a->host == "mail.google.com";
  if (!gmail && a->host != "outlook.live.com") return std::nullopt;
  const std::string_view account = gmail ? "/mail/u/" : "/mail/";
  std::string slot = "0";
  if (a->path.starts_with(account)) {
    const auto start = account.size();
    const auto end = a->path.find('/', start);
    const auto part = std::string_view(a->path).substr(start, end == a->path.npos ? end : end - start);
    if (part.empty()) {
      if (a->path.size() != start) return std::nullopt;
    } else {
      if (part.size() > 3 || !std::all_of(part.begin(), part.end(), [](char c) { return c >= '0' && c <= '9'; })) return std::nullopt;
      slot = part;
    }
  } else if (a->path != "/" && a->path != "/mail" && a->path != "/mail/") return std::nullopt;
  return gmail ? "https://mail.google.com/mail/u/" + slot + "/#inbox" :
      slot == "0" ? "https://outlook.live.com/mail/" : "https://outlook.live.com/mail/" + slot + "/inbox";
}
inline bool IsMailboxInbox(std::string_view target, std::string_view source) {
  if (MailboxTarget(source) != target) return false;
  const auto a = IntegrationUrl(source), expected = IntegrationUrl(target);
  if (!a || !expected) return false;
  const auto hash = source.find('#');
  if (expected->host == "mail.google.com")
    return a->path == expected->path && hash != source.npos && source.substr(hash) == "#inbox";
  if (hash != source.npos) return false;
  if (expected->path == "/mail/") return a->path == "/mail" || a->path == "/mail/" ||
      a->path == "/mail/0" || a->path == "/mail/0/" || a->path == "/mail/0/inbox" || a->path == "/mail/0/inbox/";
  const auto account = expected->path.substr(0, expected->path.size() - std::string_view("/inbox").size());
  return a->path == account || a->path == account + "/" || a->path == expected->path || a->path == expected->path + "/";
}
inline bool IsOutlookSignIn(std::string_view target, std::string_view destination) {
  const auto provider = LiveIntegration(target);
  const auto address = IntegrationUrl(destination);
  return provider && provider->id == "outlookMail" && address &&
      (address->host == "login.live.com" || address->host == "login.microsoftonline.com");
}
inline std::string GmailInboxSearch(std::string_view target, bool unread) {
  const auto provider = GoogleIntegration(target);
  if (!provider || provider->id != "gmail") return {};
  return provider->base + (unread ? "#search/in%3Ainbox+is%3Aunread" : "#search/in%3Ainbox");
}
inline bool IsGmailInboxSearch(std::string_view target, std::string_view source, bool unread) {
  if (GmailInboxSearch(target,unread).empty() || MailboxTarget(source) != target) return false;
  const auto hash = source.find('#');
  if (hash == source.npos || !source.substr(hash).starts_with("#search/")) return false;
  const auto query = source.substr(hash + 8);
  std::string decoded;
  for (size_t i=0;i<query.size() && decoded.size()<25;++i) {
    if (query[i]=='%' && i+2<query.size()) {
      const auto escape = query.substr(i+1,2);
      if (escape=="3A" || escape=="3a") decoded+=':';
      else if (escape=="20") decoded+=' ';
      else return false;
      i+=2;
    } else {
      decoded+=query[i]=='+'?' ':query[i];
    }
  }
  const auto page=IntegrationUrl(source), inbox=IntegrationUrl(target);
  return page && inbox && page->path==inbox->path && decoded==(unread?"in:inbox is:unread":"in:inbox");
}
inline std::optional<std::string> PreviewTarget(std::string_view saved) {
  if (auto mailbox = MailboxTarget(saved)) return mailbox;
  const auto a = IntegrationUrl(saved);
  if (!a || a->host != "calendar.google.com") return std::nullopt;
  if (const auto google = GoogleIntegration(saved)) return google->base + "r";
  if (a->path == "/" || a->path == "/calendar" || a->path == "/calendar/")
    return "https://calendar.google.com/calendar/u/0/r";
  return std::nullopt;
}
inline bool IsCalendarDayView(std::string_view source) {
  const auto provider = GoogleIntegration(source);
  const auto address = IntegrationUrl(source);
  if (!provider || provider->id != "googleCalendar" || !address) return false;
  const auto route = std::string_view(address->path).substr(IntegrationUrl(provider->base)->path.size());
  return route == "r/day" || route.starts_with("r/day/");
}
inline bool IsPreviewPage(std::string_view target, std::string_view source) {
  const auto provider = LiveIntegration(target);
  if (!provider) return false;
  if (provider->IsMail()) return IsMailboxInbox(target, source);
  const auto page = GoogleIntegration(source);
  const auto a = IntegrationUrl(source);
  if (!a || !page || page->id != "googleCalendar" || page->base != provider->base || source.find('#') != source.npos) return false;
  const auto base = IntegrationUrl(provider->base);
  const std::string_view route = std::string_view(a->path).substr(base->path.size());
  if (route == "r" || route == "r/") return true;
  for (std::string_view view : {"r/day", "r/week", "r/month", "r/schedule", "r/customweek", "r/year"}) {
    if (route == view) return true;
    if (!route.starts_with(view) || route[view.size()] != '/') continue;
    const auto date = route.substr(view.size() + 1);
    if (date.size() <= 10 && std::all_of(date.begin(),date.end(),[](char c){return c == '/' || (c >= '0' && c <= '9');})) return true;
  }
  return false;
}
inline bool GoogleMessageId(std::string_view value) {
  return value.size() >= 8 && value.size() <= 64 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
  });
}
inline bool CalendarEventId(std::string_view value) {
  return !value.empty() && value.size() <= 512 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '_' || c == '-' || c == '=' || c == ':' || c == '@' || c == '.' || c == '+';
  });
}
inline bool OutlookConversationId(std::string_view value) {
  return !value.empty() && value.size() <= 1024 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '_' || c == '-' || c == '=' || c == ':' || c == '.' || c == '+' || c == '/';
  });
}
inline bool IntegrationRowToken(const LiveProvider& provider,std::string_view value) {
  if(provider.id=="gmail")return GoogleMessageId(value);
  if(provider.id=="googleCalendar")return CalendarEventId(value);
  return provider.id=="outlookMail"&&OutlookConversationId(value);
}
inline bool IntegrationJoinUrl(std::string_view url) {
  const auto address = IntegrationUrl(url); if (!address) return false;
  return (address->host == "meet.google.com" && address->path.size() > 1) ||
      (address->host == "teams.microsoft.com" && address->path.starts_with("/l/meetup-join/")) ||
      ((address->host == "zoom.us" || address->host.ends_with(".zoom.us")) && address->path.starts_with("/j/"));
}
inline bool RecentIntegrationPage(const RecentProvider& provider, std::string_view url) {
  const auto address = IntegrationUrl(url); if (!address) return false;
  const auto& host = address->host; const auto& path = address->path;
  if (provider.id == "figma") {
    if (host != "www.figma.com" && host != "figma.com") return false;
    if (path == "/files" || path.starts_with("/files/")) return true;
    for (const std::string_view prefix : {"/design/", "/file/", "/board/", "/proto/"}) {
      if (!path.starts_with(prefix)) continue;
      const auto key = std::string_view(path).substr(prefix.size());
      const auto file = key.substr(0, key.find('/'));
      return !file.empty() && file != "new";
    }
    return false;
  }
  if (host != provider.host || !FindIntegrationSetting(provider.id)) return false;
  if (provider.id == "linear") return path.find("/issue/") != path.npos || path.find("/project/") != path.npos || path.find("/view/") != path.npos;
  if (provider.id == "confluence") return path.starts_with("/wiki/spaces/") && path.find("/pages/") != path.npos;
  if (provider.id == "notion") {
    // Published and workspace pages end with a 32-hex page ID (hyphens allowed).
    const auto end = path.find_first_of("?#"); std::string tail = path.substr(0, end);
    std::erase(tail, '-');
    return tail.size() >= 32 && std::all_of(tail.end() - 32, tail.end(), [](unsigned char c) {
      return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    });
  }
  return false;
}
// The file a Figma page belongs to ("/design/<key>/...", also /file/, /board/, /proto/), so the
// recent list shows each file once whatever node or query its visits had. Empty for other pages.
inline std::string FigmaFileKey(std::string_view url) {
  const auto address = IntegrationUrl(url);
  if (!address || (address->host != "www.figma.com" && address->host != "figma.com")) return {};
  for (const std::string_view prefix : {"/design/", "/file/", "/board/", "/proto/"}) {
    if (!address->path.starts_with(prefix)) continue;
    const auto key = std::string_view(address->path).substr(prefix.size());
    const auto file = key.substr(0, key.find('/'));
    return file.empty() || file == "new" ? std::string() : std::string(file);
  }
  return {};
}
// The file name in a Figma page title: "Abyss Cup – Figma" gives "Abyss Cup". Empty when the title is
// only Figma's own name, which Figma shows while a file is still loading.
inline std::string FigmaTitleName(std::string_view title) {
  for (const std::string_view suffix : {" \xE2\x80\x93 Figma", " \xE2\x80\x94 Figma", " - Figma"})
    if (title.ends_with(suffix)) { title.remove_suffix(suffix.size()); break; }
  while (!title.empty() && title.front() == ' ') title.remove_prefix(1);
  while (!title.empty() && title.back() == ' ') title.remove_suffix(1);
  return title == "Figma" ? std::string() : std::string(title);
}
// The file name Figma puts in the address after the key: "/design/<key>/Abyss-Cup" gives "Abyss Cup".
inline std::string FigmaUrlName(std::string_view url) {
  const auto address = IntegrationUrl(url);
  const auto key = FigmaFileKey(url);
  if (!address || key.empty()) return {};
  const auto at = address->path.find(key);
  std::string_view rest = std::string_view(address->path).substr(at + key.size());
  if (!rest.starts_with('/')) return {};
  rest.remove_prefix(1);
  rest = rest.substr(0, rest.find('/'));
  std::string name;
  for (size_t i = 0; i < rest.size(); ++i) {
    const auto hex = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    if (rest[i] == '%' && i + 2 < rest.size() && hex(rest[i + 1]) >= 0 && hex(rest[i + 2]) >= 0) {
      name += static_cast<char>(hex(rest[i + 1]) * 16 + hex(rest[i + 2])); i += 2;
    } else name += rest[i] == '-' ? ' ' : rest[i];
  }
  return name;
}
inline bool XNotificationHost(std::string_view host) {
  return host == "x.com" || host == "www.x.com" || host == "twitter.com" || host == "www.twitter.com";
}
inline bool XNotificationSite(std::string_view url) {
  const auto address = IntegrationUrl(url);
  return address && XNotificationHost(address->host);
}
enum class StartupBadgeSite { WhatsApp, X };
inline std::string_view StartupBadgeId(StartupBadgeSite site) { return site == StartupBadgeSite::X ? "twitter" : "whatsapp"; }
inline std::optional<StartupBadgeSite> StartupBadgeFor(std::string_view url, bool x_badge_hidden) {
  if (url.find_first_of("?#") != url.npos) return std::nullopt;
  const auto address = IntegrationUrl(url);
  if (!address) return std::nullopt;
  if (address->host == "web.whatsapp.com" && address->path == "/") return StartupBadgeSite::WhatsApp;
  if (!x_badge_hidden && XNotificationHost(address->host) &&
      (address->path == "/" || address->path == "/home" || address->path == "/home/"))
    return StartupBadgeSite::X;
  return std::nullopt;
}
inline std::string IntegrationForUrl(std::string_view url) {
  if (const auto target = PreviewTarget(url)) if (const auto live = LiveIntegration(*target)) return live->id;
  if (const auto recent = RecentIntegration(url)) return recent->id;
  const auto a = IntegrationUrl(url);
  if (!a) return {};
  const auto& host = a->host;
  if (XNotificationHost(host)) return "twitter";
  if (host == "web.whatsapp.com") return "whatsapp";
  if (host == "open.spotify.com") return "spotify";
  if (host == "youtube.com" || host == "www.youtube.com" || host == "m.youtube.com" || host == "music.youtube.com" || host == "youtu.be") return "youtube";
  if (host == "meet.google.com") return "googleMeet";
  if (host == "docs.new" || (host == "docs.google.com" && (a->path == "/document" || a->path.starts_with("/document/")))) return "googleDocs";
  if (host == "word.new" || host == "word.cloud.microsoft" || host == "word.office.com" ||
      ((host == "www.office.com" || host == "m365.cloud.microsoft") && a->path.starts_with("/launch/word"))) return "word";
  return {};
}
}  // namespace red
