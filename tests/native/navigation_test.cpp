#include "navigation/shortcuts.h"
#include "windows/command_actions.h"
#include "navigation/site_search.h"
#include "workspace/tab_order.h"
#include "navigation/file_url.h"
#include "integrations/providers.h"
#include "app/memory_report.h"
#include "integrations/gmail_feed.h"
#include "integrations/whatsapp_title.h"
#include <cstdio>
int main() {
  int failures = 0;
  auto check = [&](bool value, const char* name) { if (!value) { std::fprintf(stderr, "%s\n", name); ++failures; } };
  check(red::FileUrlFromGenericPath(u8"C:/Users/me/a b#1.pdf") == "file:///C:/Users/me/a%20b%231.pdf", "dropped file path to URL");
  check(red::FileUrlFromGenericPath(u8"D:/книга.pdf") == "file:///D:/%D0%BA%D0%BD%D0%B8%D0%B3%D0%B0.pdf", "UTF-8 file names");
  check(red::FileUrlFromGenericPath(u8"//server/share/a.pdf").empty() && red::FileUrlFromGenericPath(u8"a.pdf").empty(),
        "UNC and relative paths have no local file URL");
  using red::Shortcut;
  const auto gmail=red::GoogleIntegration("https://mail.google.com/mail/u/1/#inbox");
  const auto calendar=red::GoogleIntegration("https://calendar.google.com/calendar/u/2/r");
  check(gmail && gmail->base=="https://mail.google.com/mail/u/1/" && calendar && calendar->id=="googleCalendar","Google cards preserve account routes");
  for(const auto* url:{"https://mail.google.com.evil.test/mail/u/0/","https://mail.google.com/mail/","https://mail.google.com/mail/u/user/","https://user@mail.google.com/mail/u/0/"})check(!red::GoogleIntegration(url),"reject ambiguous/spoofed Google accounts");
  check(red::GoogleMessageId("1234567890abcdef")&&!red::GoogleMessageId("javascript:alert(1)")&&!red::CalendarEventId("x');alert(1)//"),"bounded Google row tokens");
  const auto outlook=red::LiveIntegration("https://outlook.live.com/mail/0/inbox");
  check(red::MailboxTarget("https://mail.google.com/mail/u/2/#sent")=="https://mail.google.com/mail/u/2/#inbox"&&
        red::MailboxTarget("https://mail.google.com/")=="https://mail.google.com/mail/u/0/#inbox"&&
        red::MailboxTarget("https://outlook.live.com/mail/1/deleteditems?view=wide")=="https://outlook.live.com/mail/1/inbox",
        "mailbox preview normalizes folder while preserving the saved account");
  check(!red::MailboxTarget("https://docs.google.com/document/d/1")&&!red::MailboxTarget("https://outlook.live.com/calendar/0")&&
        red::IsMailboxInbox("https://mail.google.com/mail/u/2/#inbox","https://mail.google.com/mail/u/2/?ui=2#inbox")&&
        !red::IsMailboxInbox("https://mail.google.com/mail/u/2/#inbox","https://mail.google.com/mail/u/1/#inbox")&&
        !red::IsMailboxInbox("https://outlook.live.com/mail/1/inbox","https://outlook.live.com/mail/1/junkemail"),
        "background reads cannot switch account or use the current folder");
  check(red::PreviewTarget("https://outlook.live.com/mail/0/inbox")=="https://outlook.live.com/mail/"&&
        red::IsPreviewPage("https://outlook.live.com/mail/","https://outlook.live.com/mail/")&&
        red::IsPreviewPage("https://outlook.live.com/mail/","https://outlook.live.com/mail/0/inbox")&&
        !red::IsPreviewPage("https://outlook.live.com/mail/","https://outlook.live.com/mail/1/inbox"),
        "consumer Outlook starts at its working root without crossing accounts");
  check(red::IsOutlookSignIn("https://outlook.live.com/mail/","https://login.live.com/oauth20_authorize.srf") &&
        red::IsOutlookSignIn("https://outlook.live.com/mail/","https://login.microsoftonline.com/common/oauth2/authorize") &&
        !red::IsOutlookSignIn("https://outlook.live.com/mail/","https://login.live.com.evil.test/") &&
        !red::IsOutlookSignIn("https://mail.google.com/mail/u/0/#inbox","https://login.live.com/"),
        "Outlook sign-in detection is exact and provider-scoped");
  check(red::PreviewTarget("https://calendar.google.com/calendar/")=="https://calendar.google.com/calendar/u/0/r"&&
        red::PreviewTarget("https://calendar.google.com/calendar/u/2/r/week/2025/5/20")=="https://calendar.google.com/calendar/u/2/r"&&
        red::IsPreviewPage("https://calendar.google.com/calendar/u/2/r","https://calendar.google.com/calendar/u/2/r/week/2025/5/20")&&
        !red::IsPreviewPage("https://calendar.google.com/calendar/u/2/r","https://calendar.google.com/calendar/u/2/r/eventedit")&&
        !red::IsPreviewPage("https://calendar.google.com/calendar/u/2/r","https://calendar.google.com/calendar/u/1/r"),
        "background calendar accepts view redirects only in the saved account");
  const std::string gmail_inbox="https://mail.google.com/mail/u/2/#inbox";
  check(red::IsGmailInboxSearch(gmail_inbox,red::GmailInboxSearch(gmail_inbox,true),true)&&
        red::IsGmailInboxSearch(gmail_inbox,red::GmailInboxSearch(gmail_inbox,false),false)&&
        red::IsGmailInboxSearch(gmail_inbox,"https://mail.google.com/mail/u/2/#search/in%3Ainbox%20is%3Aunread%20newer_than%3A7d",true)&&
        !red::IsGmailInboxSearch(gmail_inbox,"https://mail.google.com/mail/u/1/#search/in%3Ainbox+is%3Aunread+newer_than%3A7d",true)&&
        !red::IsGmailInboxSearch(gmail_inbox,"https://mail.google.com/mail/u/2/#search/is%3Aunread",true),
        "Gmail background uses a fixed unread-inbox query in the saved account");
  check(red::GmailInboxSearch(gmail_inbox,true).ends_with("#search/in%3Ainbox+is%3Aunread+newer_than%3A7d") &&
        !red::IsGmailInboxSearch(gmail_inbox,"https://mail.google.com/mail/u/2/#search/in%3Ainbox+is%3Aunread",true),
        "Gmail unread search covers the last week only; the old unbounded query is not accepted");
  check(outlook&&outlook->id=="outlookMail"&&outlook->IsMail()&&
        red::IntegrationRowToken(*outlook,"AAMk/Owned+Row==")&&!red::IntegrationRowToken(*outlook,"with spaces")&&
        !red::IntegrationRowToken(*outlook,std::string(1025,'a')),"Outlook accepts bounded opaque conversation IDs");
  for(const auto* url:{"https://outlook.live.com/calendar/","https://outlook.live.com/mailbox/","https://outlook.office.com/mail/",
                      "https://outlook.live.com.evil.test/mail/","https://user@outlook.live.com/mail/","https://outlook.live.com:444/mail/"})
    check(!red::LiveIntegration(url),"reject unsupported or spoofed Outlook sources");
  check(red::IntegrationJoinUrl("https://meet.google.com/abc-defg-hij")&&!red::IntegrationJoinUrl("https://meet.google.com.evil.test/abc")&&!red::IntegrationJoinUrl("https://user:pass@meet.google.com/abc")&&!red::IntegrationJoinUrl("http://zoom.us/j/123"),"strict meeting destinations");
  check(red::CommandDocumentUrl("newGoogleDoc")=="https://docs.new/" && red::CommandDocumentUrl("newWordDoc")=="https://word.new/", "fixed document creation URLs");
  check(red::CommandDocumentUrl("newFigmaDesign")=="https://figma.new/" && red::CommandShortcut("newFigmaDesign")==Shortcut::NewTab,
        "Figma Design opens the official fixed destination");
  check(red::CommandDocumentUrl("newNote","word")=="https://word.new/" && red::CommandDocumentUrl("newNote","googleDocs")=="https://docs.new/", "New Note follows the selected document provider");
  check(red::CommandDocumentUrl("https://evil.test/").empty() && red::CommandShortcut("newGoogleDoc")==Shortcut::NewTab, "document commands cannot supply arbitrary URLs");
  const red::IntegrationSettings preferences{{"googleDocs", false}, {"linear", true}, {"figma", false}};
  check(red::EnabledDocumentUrl("newNote", "googleDocs", preferences)=="https://word.new/" &&
      red::EnabledDocumentUrl("newFigmaDesign", "googleDocs", preferences).empty() &&
      red::EnabledDocumentUrl("newNote", "googleDocs", {{"googleDocs", false}, {"word", false}}).empty(), "Document actions honor provider switches");
  {
    const std::string xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?><feed version=\"0.3\" xmlns=\"http://purl.org/atom/ns#\"><title>Gmail - Inbox</title>"
        "<fullcount>6431</fullcount><entry><title>Black Friday &amp; more: $29 &#x2014; &#8220;deal&#8221;</title>"
        "<summary>preview</summary><link rel=\"alternate\" href=\"https://mail.google.com/mail/u/0?account_id=a@b.c&amp;message_id=1a1c247f453e3a79&amp;view=conv&amp;extsrc=atom\" type=\"text/html\"/>"
        "<modified>2026-11-09T21:47:38Z</modified><issued>2026-11-09T21:47:38Z</issued><author><name>Windscribe</name><email>n@w.com</email></author></entry>"
        "<entry><title>No id</title><issued>2026-11-09T21:47:38Z</issued><author><name>X</name></author></entry>"
        "<entry><title>Bad date</title><link href=\"x?message_id=1a196d6a8ab8833e&amp;v\"/><issued>yesterday</issued><author><name>X</name></author></entry>"
        "<entry><title>\xD0\x9E\xD0\xBD\xD0\xBE\xD0\xB2\xD0\xBB\xD0\xB5\xD0\xBD\xD0\xBD\xD1\x8F</title><link href=\"x?message_id=1a19444f8613e81e&amp;v\"/>"
        "<issued>2026-10-31T23:21:50Z</issued><author><name>Binance</name></author></entry></feed>";
    const auto feed = red::ParseGmailFeed(xml);
    check(feed && feed->unread == 6431 && feed->entries.size() == 2, "Gmail feed: count and entries with an ID and a date");
    check(feed && feed->entries[0].title == "Black Friday & more: $29 \xE2\x80\x94 \xE2\x80\x9C" "deal\xE2\x80\x9D" &&
          feed->entries[0].sender == "Windscribe" && feed->entries[0].message_id == "1a1c247f453e3a79" &&
          feed->entries[0].issued_ms == 1794260858000LL, "Gmail feed: entities, sender, message ID and UTC time");
    check(feed && feed->entries[1].title == "\xD0\x9E\xD0\xBD\xD0\xBE\xD0\xB2\xD0\xBB\xD0\xB5\xD0\xBD\xD0\xBD\xD1\x8F", "Gmail feed: UTF-8 titles");
    check(!red::ParseGmailFeed("<!doctype html><title>Sign in - Google Accounts</title>") &&
          !red::ParseGmailFeed("<feed><fullcount>12x</fullcount></feed>"), "Gmail feed: a sign-in page or bad count is not a feed");
    check(red::gmail_feed_detail::IsoUtc("1970-01-01T00:00:00Z") == 0 && red::gmail_feed_detail::IsoUtc("2000-03-01T00:00:01Z") == 951868801000LL &&
          !red::gmail_feed_detail::IsoUtc("2026-11-09 21:47:38"), "Gmail feed: ISO dates");
  }
  check(red::WhatsAppTitleCount("(3) WhatsApp") == 3 && red::WhatsAppTitleCount("(128) WhatsApp") == 128 &&
        red::WhatsAppTitleCount("WhatsApp") == 0, "WhatsApp title counts");
  check(!red::WhatsAppTitleCount("") && !red::WhatsAppTitleCount("web.whatsapp.com") && !red::WhatsAppTitleCount("(3) WhatsApp Web") &&
        !red::WhatsAppTitleCount("(x) WhatsApp") && !red::WhatsAppTitleCount("() WhatsApp") && !red::WhatsAppTitleCount("(3)WhatsApp"),
        "Other WhatsApp titles are not counts");
  // CEF frame identifiers are "<renderer host ID in hex>-<frame token>" (cef/libcef/common/frame_util.cc).
  check(red::RendererHostId("1A-0123456789ABCDEF0123456789ABCDEF") == 26 && red::RendererHostId("7-AB") == 7 &&
        red::RendererHostId("") == -1 && red::RendererHostId("-AB") == -1 && red::RendererHostId("1G-AB") == -1 &&
        red::RendererHostId("123456789-AB") == -1, "renderer host IDs");
  for (const auto* id : {"spotify", "youtube"})
    check(red::FindIntegrationSetting(id) && !red::FindIntegrationSetting(id)->listed, "Spotify and YouTube are readable but not offered as switches");
  check(red::FindIntegrationSetting("googleMeet") && red::FindIntegrationSetting("googleMeet")->listed, "Google Meet keeps its switch");
  check(!red::IntegrationEnabled({}, "linear") && !red::IntegrationEnabled(preferences, "linear") &&
      !red::IntegrationEnabled({}, "github"), "Saved overrides cannot enable paused or unimplemented providers");
  check(red::IntegrationForUrl("https://linear.app/team/issue/ID-1").empty() && red::IntegrationForUrl("https://x.com/home") == "twitter" &&
      red::IntegrationForUrl("https://www.youtube.com/watch?v=example") == "youtube" && red::IntegrationForUrl("https://open.spotify.com/") == "spotify" &&
      red::IntegrationForUrl("https://meet.google.com/fixture") == "googleMeet" && red::IntegrationForUrl("https://www.youtube.com.evil.test/").empty(), "Pin toggles resolve only known providers");
  const auto figma = red::RecentIntegration("https://www.figma.com/files");
  check(figma && red::RecentIntegrationPage(*figma,"https://www.figma.com/design/owned/Fixture"),"Figma design page");
  check(figma && red::RecentIntegrationPage(*figma,"https://www.figma.com/files/team/123/drafts") &&
        !red::RecentIntegrationPage(*figma,"https://figma.com/design/new") &&
        !red::RecentIntegrationPage(*figma,"https://figma.com.evil.test/files/team/123/drafts"),"Figma Drafts is allowed alongside files, creation and spoof hosts are excluded");
  for (const auto* url : {"https://notion.so/", "https://team.notion.site/", "https://linear.app/team", "https://team.atlassian.net/wiki/"})
    check(!red::RecentIntegration(url) && red::IntegrationForUrl(url).empty(), "Paused provider is unavailable in both the card and pin menu");
  check(red::XNotificationSite("https://x.com/home") && red::XNotificationSite("https://twitter.com/notifications") &&
        !red::XNotificationSite("https://x.com.evil.test/") && !red::XNotificationSite("https://user@x.com/") &&
        !red::XNotificationSite("https://x.com:444/"), "X badges require an authentic origin");
  using red::StartupBadgeSite;
  check(red::StartupBadgeFor("https://web.whatsapp.com/", false) == StartupBadgeSite::WhatsApp &&
        red::StartupBadgeFor("https://web.whatsapp.com", true) == StartupBadgeSite::WhatsApp,
        "WhatsApp startup accepts the standard home URL");
  for (const auto* url : {"https://web.whatsapp.com/send", "https://web.whatsapp.com/?phone=123", "https://web.whatsapp.com/#chat",
      "https://web.whatsapp.com:444/", "https://web.whatsapp.com.evil.test/", "https://user@web.whatsapp.com/", "http://web.whatsapp.com/"})
    check(!red::StartupBadgeFor(url, false), "WhatsApp startup excludes links and other origins");
  for (const auto* url : {"https://x.com/home", "https://www.x.com/", "https://twitter.com", "https://www.twitter.com/home/"}) {
    check(red::StartupBadgeFor(url, false) == StartupBadgeSite::X, "X startup accepts the standard home URL");
    check(!red::StartupBadgeFor(url, true), "Hidden X badges skip startup loading");
  }
  for (const auto* url : {"https://x.com/notifications", "https://x.com/home?tab=following", "https://x.com/home#post",
      "https://x.com/i/flow/login", "https://x.com:444/", "https://x.com.evil.test/", "https://user@x.com/", "http://x.com/"})
    check(!red::StartupBadgeFor(url, false), "X startup excludes links and other origins");
  for(const auto* url:{"http://figma.com/","https://figma.com.evil.test/","https://user:secret@figma.com/","https://figma.com:444/","https://team.atlassian.net/wiki-other"})check(!red::RecentIntegration(url),"integration origin policy");
  check(figma && !red::RecentIntegrationPage(*figma,"https://www.figma.com/login"),"exclude login pages");
  check(red::FigmaFileKey("https://www.figma.com/design/QPMgAbc123/Abyss-Cup?node-id=0-1") == "QPMgAbc123" &&
        red::FigmaFileKey("https://www.figma.com/design/QPMgAbc123?fuid=1") == "QPMgAbc123" &&
        red::FigmaFileKey("https://www.figma.com/proto/FxzM9/Deck?node-id=0-1") == "FxzM9" &&
        red::FigmaFileKey("https://www.figma.com/file/new?fuid=1").empty() &&
        red::FigmaFileKey("https://www.figma.com/files/team/123/drafts").empty() &&
        red::FigmaFileKey("https://figma.com.evil.test/design/QPMgAbc123").empty(), "Figma file keys ignore node, query and slug");
  check(red::FigmaTitleName("Abyss Cup \xE2\x80\x93 Figma") == "Abyss Cup" && red::FigmaTitleName("Abyss Cup - Figma") == "Abyss Cup" &&
        red::FigmaTitleName("Figma").empty() && red::FigmaTitleName("  Figma ").empty() &&
        red::FigmaTitleName("Figma Design System \xE2\x80\x93 Figma") == "Figma Design System", "Figma titles drop Figma's own suffix");
  check(red::FigmaUrlName("https://www.figma.com/design/QPMgAbc123/Abyss-Cup?node-id=0-1") == "Abyss Cup" &&
        red::FigmaUrlName("https://www.figma.com/design/QPMgAbc123/%D0%9C%D0%B0%D0%BA%D0%B5%D1%82") == "\xD0\x9C\xD0\xB0\xD0\xBA\xD0\xB5\xD1\x82" &&
        red::FigmaUrlName("https://www.figma.com/design/QPMgAbc123?fuid=1").empty(), "Figma file names come from the address slug");
  check(red::ShortcutKey('L', true, false, false) == Shortcut::Address, "Ctrl+L");
  check(red::ShortcutKey(9, true, true, false) == Shortcut::PreviousTab, "reverse tab cycle");
  check(red::ShortcutKey(37, false, false, true) == Shortcut::Back, "history navigation");
  check(red::ShortcutKey(115, false, false, true) == Shortcut::CloseWindow, "Alt+F4 routes to root coordinator instead of closing one Chrome child");
  check(red::ShortcutKey('R', true, true, false) == Shortcut::ReloadBypass, "cache-bypassing reload");
  check(red::ShortcutKey(123, false, false, false) == Shortcut::DevTools, "F12 opens page tools");
  check(red::ShortcutKey('I', true, true, false) == Shortcut::DevTools, "Ctrl+Shift+I opens page tools");
  check(!red::ShortcutKey(123, false, true, false) && !red::ShortcutKey('I', true, true, true), "DevTools modifiers stay exact");
  check(red::ShortcutNamed("devTools") == Shortcut::DevTools, "typed DevTools command");
  check(!red::ShortcutKey('T', true, false, true), "AltGr is not a browser shortcut");
  check(!red::ShortcutKey('W', true, true, false), "Ctrl+Shift+W is not accidentally close-tab");
  check(!red::ShortcutNamed("eval"), "no arbitrary native command capability");
  for (const auto name : {"settings", "downloads", "historyPage", "reopenClosed", "sidebar", "newWindow", "newPrivateWindow",
                          "reload", "reloadBypass", "pin", "back", "forward", "devTools", "print", "fullscreen"})
    check(red::CommandShortcut(name) == red::ShortcutNamed(name), "command action allowlist");
  for (const auto name : {"eval", "newTab", "address", "focusPage", "closeWindow", "space1", "Settings", ""})
    check(!red::CommandShortcut(name), "command renderer cannot invoke unlisted shortcuts");
  check(!red::CommandAvailable(Shortcut::Reload, false, false, false, false), "page action needs a live page");
  check(!red::CommandAvailable(Shortcut::Pin, true, true, false, false), "standalone tabs cannot pin");
  check(!red::CommandAvailable(Shortcut::Back, true, false, false, false), "back needs history");
  check(red::CommandAvailable(Shortcut::Back, true, false, true, false), "available back action");
  check(red::CommandAvailable(Shortcut::Settings, false, true, false, false), "settings works in an empty window");
  check(red::CommandShortcut("siteSearchSettings") == Shortcut::Settings, "site-search settings has a narrow native settings route");
  check(red::ValidSiteSearchActivation("spaceOrTab") && red::ValidSiteSearchActivation("tab") && !red::ValidSiteSearchActivation("enter"), "site-search activation choices");
  check(red::ValidSiteSearchFields({"Blog", "blog.example.com", "https://example.test/?q=%s"}), "domain shortcut");
  check(red::ValidSiteSearchFields({"Blog", "@blog", "https://example.test/?q=%s"}), "at shortcut");
  for (const auto& site : red::DefaultSiteSearches()) check(red::ValidSiteSearchFields(site), "default site-search templates");
  check(red::ValidSiteSearchFields({"Owned", "owned", "http://127.0.0.1:8080/search?q=%s"}), "local fixture search template");
  for (const auto url : {"javascript:%s", "sol://ui/%s", "file:///C:/%s", "https://%s.example/", "https://example.test:%s/",
                         "https://example.test/no-token", "https://example.test/?q=%s&x=%s", "https://example.test/?q=%s&bad=%zz"})
    check(!red::ValidSiteSearchFields({"Owned", "owned", url}), "unsafe site-search template rejected");
  check(!red::ValidSiteSearchFields({"Owned", "two words", "https://example.test/?q=%s"}), "shortcut cannot include whitespace");
  std::vector<red::TreeNode> nodes;
  auto node = [&](const char* id, const char* parent, int position, bool folder, bool pinned, bool favorite, bool archived) {
    red::TreeNode n; n.id = id; n.parent = parent; n.position = position; n.folder = folder;
    n.pinned = pinned; n.favorite = favorite; n.archived = archived; n.expanded = false; nodes.push_back(n);
  };
  node("today", "", 0, false, false, false, false);
  node("folder", "", 3, true, true, false, false);
  node("nested", "folder", 0, true, true, false, false);
  node("nested-tab", "nested", 0, false, true, false, false);
  node("favorite", "", 6, false, true, true, false);
  node("archived", "", 1, false, false, false, true);
  for (auto& n : nodes) if (n.parent.empty() && !n.favorite) n.space = "work";
  node("other-space", "", 2, false, false, false, false); nodes.back().space = "home";
  check(red::OrderedTabs(nodes, "work") == std::vector<std::string>{"favorite", "nested-tab", "today"},
        "tab cycle follows sidebar sections and collapsed descendants, excludes archive and other spaces");
  check(red::OrderedTabs(nodes, "home") == std::vector<std::string>{"favorite", "other-space"}, "favorites are shared by every space");
  check(red::OrderedTabs({}, "work").empty(), "empty workspace");
  check(red::CollapsedFolderContaining(nodes, "nested-tab") == "folder", "the outer collapsed ancestor owns the visible peek");
  check(red::OrderedTabs(nodes, "work", "folder") == std::vector<std::string>{"favorite", "today"},
        "closing a collapsed peek excludes all descendants from its fallback");
  check(red::OrderedTabs({nodes[1], nodes[2], nodes[3]}, "work", "folder").empty(),
        "a collapsed folder by itself offers no replacement page");
  check(red::OrderedTabs(nodes, "work", {}, "default", true) == std::vector<std::string>{"favorite", "today"},
        "closing a page elsewhere skips pages hidden in collapsed folders");
  nodes[1].expanded = true;
  check(red::OrderedTabs(nodes, "work", {}, "default", true) == std::vector<std::string>{"favorite", "today"},
        "a collapsed folder inside an expanded one still hides its pages");
  nodes[2].expanded = true;
  check(red::OrderedTabs(nodes, "work", {}, "default", true) == std::vector<std::string>{"favorite", "nested-tab", "today"},
        "pages in expanded folders remain candidates");
  nodes[1].expanded = nodes[2].expanded = false;
  nodes[1].expanded = true;
  check(red::CollapsedFolderContaining(nodes, "nested-tab") == "nested", "an expanded outer folder leaves the inner peek independent");
  nodes[2].expanded = true;
  check(red::CollapsedFolderContaining(nodes, "nested-tab").empty(), "an expanded path uses the regular close order");
  {
    // Today archive targets: Close Others / Close Below / Clear.
    red::TreeSnapshot tree;
    auto tab = [&](const char* id, int position, bool pinned = false, const char* space = "work") {
      red::TreeNode n; n.id = id; n.position = position; n.pinned = pinned; n.space = space; tree.nodes.push_back(n);
    };
    tab("t0", 0); tab("t1", 1); tab("t2", 2); tab("t3", 3); tab("pin", 4, true); tab("away", 0, false, "home");
    using V = std::vector<std::string>;
    check(red::TodayArchiveTargets(tree, "t1", false, false, "t1") == V{"t0", "t2", "t3"}, "close others keeps only the chosen tab");
    check(red::TodayArchiveTargets(tree, "t1", true, false, "t1") == V{"t2", "t3"}, "close below archives later Today tabs");
    check(red::TodayArchiveTargets(tree, "t0", false, true, "t2") == V{"t0", "t1", "t3"}, "Clear keeps the active Today tab");
    check(red::TodayArchiveTargets(tree, "t0", false, true, "pin") == V{"t0", "t1", "t2", "t3"}, "Clear archives every Today tab when a pinned page is active");
    check(red::TodayArchiveTargets(tree, "t0", false, true, "") == V{"t0", "t1", "t2", "t3"}, "Clear without an active page archives all");
    tree.splits.push_back({"t1", "t3"});
    check(red::TodayArchiveTargets(tree, "t0", false, true, "t3") == V{"t0", "t2"}, "Clear keeps both panes of the active split");
    check(red::TodayArchiveTargets(tree, "t2", false, true, "t2") == V{"t0", "t1", "t3"}, "an inactive split is cleared");
    check(red::TodayArchiveTargets(tree, "pin", false, true, "t2").empty(), "a pinned tab is not a Today target");
    tree.nodes.resize(1);
    check(red::TodayArchiveTargets(tree, "t0", false, true, "t0").empty(), "nothing to clear beside the active tab");
  }
  return failures ? 1 : 0;
}
