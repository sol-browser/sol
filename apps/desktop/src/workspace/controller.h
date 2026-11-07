#pragma once
#include <functional>
#include <map>
#include <memory>
#include <set>
#include "app/state.h"
#include "trust_policy.h"
#include "storage/tree_store.h"
#include "include/cef_thread.h"

namespace red {
class WorkspaceController : public std::enable_shared_from_this<WorkspaceController> {
 public:
  using Reply = std::function<void(int, const std::string&, CefRefPtr<CefDictionaryValue>)>;
  explicit WorkspaceController(std::weak_ptr<State> state) : state_(std::move(state)) {}
  void Start();
  void Stop();
  void Handle(CefRefPtr<CefDictionaryValue> request, Reply done);
  CefRefPtr<CefDictionaryValue> Snapshot() const;
  CefRefPtr<CefDictionaryValue> ThemeSnapshot() const;
  // Layout only (live sidebar/DevTools divider drags): sidebar width and the
  // DevTools dock, no tree. The renderer merges it into its last snapshot.
  CefRefPtr<CefDictionaryValue> LayoutSnapshot() const;
  bool ready() const { return ready_ && !shutting_down_; }
  std::string ActiveSpaceName() const {
    const auto* space=tree_.ActiveSpace();return space?space->name:"Your space";
  }
  void SetObserver(std::function<void(CefRefPtr<CefDictionaryValue>)> observer);
  void ReceiveSharedTree(const TreeSnapshot& tree);
  bool MoveToNewWindow(const std::string& id);
  void DetachedReady(const std::string& id);
  std::optional<TreeNode> DetachedSeed(const std::string& id) const;
  void HandleTabDrag(CefRefPtr<CefDictionaryValue> request, Reply done);
  struct SplitDrop { TreeCommand command; CefRect preview; };
  std::optional<SplitDrop> SplitDropAt(const std::string& id, POINT screen);
  void OpenMovedTab(const std::string& id, const std::string& url);
  // Live tab transfer between windows (docs/multi-window.md): the source lets
  // go of a page without closing it (its entry is removed when |remove_entry|,
  // a Today tab leaving this window's data), and the destination adopts that
  // same browser for one of its entries. Null/false when the page cannot move
  // live right now (loading/closing, Link Peek, split); callers then reload.
  CefRefPtr<CefBrowser> ReleaseForTransfer(const std::string& id, bool remove_entry);
  bool CanTransfer(const std::string& id) const;
  bool AdoptPage(CefRefPtr<CefBrowser> browser, const std::string& node_id);
  // A tab's Link Peek moves with it: the source parks an open preview and
  // lets go of its page (false: it has one that cannot move now); the
  // destination keeps it parked for the tab, so it reopens when shown.
  struct MovingPeek { std::string id, url; CefRefPtr<CefBrowser> browser; int origin_x = 0, origin_y = 0; };
  bool ReleasePeekForTransfer(const std::string& opener, std::optional<MovingPeek>& peek);
  void AdoptPeek(const std::string& opener, const MovingPeek& peek);
  void OpenDetachedCopy(const TreeNode& seed, std::function<void()> ready);
  CefRefPtr<CefBrowser> LiveBrowser(const std::string& id) const;
  void OnCreated(CefRefPtr<CefBrowser> browser, const std::string& node_id);
  void OnClosed(int browser_id, const std::string& last_url);
  void OnPageChanged(CefRefPtr<CefBrowser> browser);
  void OnPageFocused(CefRefPtr<CefBrowser> browser);
  bool RouteLinkPeek(CefRefPtr<CefBrowser> source,const std::string& url,bool force);
  bool IsLinkPeekBrowser(int browser) const;
  bool HasLinkPeek() const { return !link_peek_id_.empty(); }
  bool CanSplitLinkPeek() const;
  void DismissLinkPeek(std::function<void(bool)> after = {});
  void OnLoadFinished(CefRefPtr<CefBrowser> browser);
  // Native page metadata is untrusted text; automatic titles persist separately
  // from explicit user names.
  void OnTitleChanged(CefRefPtr<CefBrowser> browser);
  void OnFaviconChanged(CefRefPtr<CefBrowser> browser, const std::vector<CefString>& urls);
  void OnDocumentStarted(int browser, const CefString& url);
  void OpenNewTab();
  void FocusAddress();
  // Asks the docked sidebar to start one of its own flows: newSpace, editSpace, newFolder.
  void RequestSidebar(const std::string& action);
  void CloseActive();
  void CycleTab(bool reverse);
  void OnMediaResult(int browser, int id, const std::string& json);
  // CefDisplayHandler::OnMediaSessionChanged (engine patch 09) for a workspace page.
  void OnMediaSession(CefRefPtr<CefBrowser> browser, CefRefPtr<CefDictionaryValue> info);
  void OnMediaArtwork(int browser, const std::string& url, const std::string& png);
  // The space at |index| in switcher order, if there is one.
  void SwitchSpaceAt(int index);
  // |initiator|: the website page whose link this is (http/https), so the
  // new page loads as that page's link, not as a typed address.
  // |opener|: the page the link was clicked in; its Today tab gets the new tab right below it.
  void OpenLink(const std::string& url, bool foreground, Reply done = {}, const std::string& initiator = "",
                CefRefPtr<CefBrowser> opener = nullptr);
  void QueueExternalUrls(const std::vector<std::string>& urls);
  // A local file the person opened (dropped on a page): file:/// only.
  void OpenFile(const std::string& url, bool foreground);
  // A new tab for an address the person typed: a website or a local file.
  void OpenTyped(const std::string& url, Reply done);
  // Reopens a page an extension asked Chrome to open as a foreground Today tab.
  bool AdoptExtensionPage(const std::string& url);
  // Opens (or activates) an internal page tab (history, downloads).
  void OpenInternalPage(std::string_view url, const std::string& title);
  void OpenHistoryPage() { OpenInternalPage(kHistoryUrl, "History"); }
  void OpenDownloadsPage() { OpenInternalPage(kDownloadsUrl, "Downloads"); }
  // Ctrl+Shift+T: closed Today tabs are archived; restore the latest one.
  void ReopenClosed();
  // Sidebar menu: duplicate a tab as a new Today tab; mute a live tab (this
  // session only). Return an HTTP-like status and an error for the shell.
  int Duplicate(const std::string& id, std::string& error);
  // Sidebar tab menu actions (tab_menu_actions.cpp).
  int CopyTabLink(const std::string& id, std::string& error);
  int SetPinnedPage(const std::string& id, std::string url, const std::string& title, std::string& error);  // Empty url: current page.
  // Archives the Today tabs of |id|'s space: the others, those below it, or (|all|, the divider's Clear) every one.
  int ArchiveTodayTabs(const std::string& id, bool below, std::string& error, bool all = false);
  int MoveToNewSpace(const std::string& id, std::string& error);
  int SetMuted(const std::string& id, bool muted, std::string& error);
  // Visible, user-dismissable message in the trusted UI.
  void Notify(const std::string& text);
  void SetSidebarVisible(bool visible, bool live = false);
  void SetSidebarWidth(int width, bool save, Reply done);
  // Live preview while dragging (save=false), persisted on release. Also
  // re-resolves the System mode against Windows' current app mode.
  void SetTheme(const Theme& theme, bool save, Reply done, const std::string& space = "");
  // Drops an unsaved theme preview: the active space's saved theme shows again.
  void RevertThemePreview();
  static void ApplyTheme(const std::shared_ptr<State>& state);
  // The window view's own background: transparent behind a system backdrop.
  static cef_color_t WindowBackground(const std::shared_ptr<State>& state);
  // {method, hue, intensity, mode: "light"|"dark"|"system", noise, save}.
  static bool ParseThemeCommand(CefRefPtr<CefDictionaryValue> d, Theme& theme, bool& save);
  void TogglePin();
  void Navigate(const std::string& url, Reply done);
  void Close(const std::string& id, bool archive, Reply done = {});
  void CancelClose(int browser_id);
  bool PrepareShutdown(std::function<void(bool)> done);
  void CancelShutdown();
  bool WindowClosing() const { return shutting_down_; }
  bool OnCrashed(CefRefPtr<CefBrowser> browser);
  void Reload(bool bypass = false);
  void NavigateHistory(bool forward);
  bool SplitResizeHovered(const std::string& anchor) const;
  bool AutoArchiveBusy() const;
  bool AutoArchiveProtected(const std::string& id) const;
  // Memory unloading: a page may close without losing where it is (Today
  // keeps its current address; Pinned/Favorites only at their saved one).
  bool UnloadKeepsPlace(const std::string& id) const;
  void AutoArchiveUnload(const std::string& id, std::function<void(bool)> done);
  bool AutoArchiveClosing(int browser) const;
  void ProtectMediaRequest(int browser) { auto_archive_media_.insert(browser); }
  void TouchAudibleActivity(int browser);
  bool IsClosing() const { return !closing_.empty(); }
  void AttachContent() { ApplyStartup(); EnsureActive(); Publish(); DrainExternalUrls(); }
  // Re-send the snapshot after browser-owned state outside the tree changed.
  void Refresh() { Publish(); }
  void ImportZen(const TreeSnapshot& incoming, Reply done);
 private:
  void ApplyStartup();
  bool HandleLinkPeek(CefRefPtr<CefDictionaryValue> request,Reply done);
  CefRefPtr<CefDictionaryValue> DescribeLinkPeek() const;
  void FinishLinkPeekClose();
  bool ParkLinkPeek();
  void ResumeLinkPeek();
  void CloseParkedPeek(const std::string& source);
  bool IsPeekPage(const std::string& id) const;
  // Sidebar media player (media_controller.cpp).
  void UpdateMedia();
  void PollMedia();
  CefRefPtr<CefListValue> DescribeMedia() const;
  bool HandleMedia(CefRefPtr<CefDictionaryValue> request, Reply done);
  const SplitGroup* SelectedSplit() const;
  void FinishTabDrag(const std::string& id, bool dropped);
  struct TabDrag {
    std::string id, previous, space;
    bool restored = false;
  };
  std::optional<TabDrag> tab_drag_;
  void EnsurePage(const std::string& id);
  bool HandleSplit(CefRefPtr<CefDictionaryValue> request, Reply done);
  CefRefPtr<CefDictionaryValue> DescribeSplitView() const;
  void ResizeSplitTick(unsigned generation);
  void Dispatch(TreeCommand command, Reply done, bool internal = false);
  void Complete(TreeResult result, Reply done, bool shared = false);
  void CompleteMetadata(TreeResult result, Reply done);
  void UnloadForMove(const std::string& id);
  void RemoveDetachedToday(const std::string& id);
  void EnsureActive();
  void DrainExternalUrls();
  // Gives the page the space beside a docked sidebar of |width|.
  void LayoutSidebar(double width, bool final);
  // Probe only: sidebar layout evidence once no page is being revealed.
  static void RecordSidebarLayout(std::weak_ptr<State> weak, int attempts);
  void Publish();
  void PublishTheme();
  void PublishLayout();
  void SavePageTitle(const std::string& id);
  void FlushPageTitle(const std::string& id);
  void FinishClose(const std::string& id, bool archive, Reply done, const std::string& last_url = "");
  void RemoveFolder(TreeCommand command, Reply done);
  void CancelFolderRemoval();
  struct FolderRemoval;
  std::shared_ptr<FolderRemoval> folder_removal_;
  std::set<std::string> removing_folder_nodes_;
  void OpenPage(const std::string& url, const std::string& title, bool foreground, Reply done, const std::string& initiator = "",
                const std::string& after = "");
  // Creates the live page for |id|; a page opened from a website's link starts
  // empty and then loads |url| with that page as initiator (engine patch 02).
  bool CreateLive(const std::string& url, const std::string& id);
  bool CanOpen() const;
  std::weak_ptr<State> state_;
  CefRefPtr<CefThread> worker_;
  std::shared_ptr<AppSession> session_;
  std::shared_ptr<WorkspaceData> data_;
  TreeSnapshot tree_;
  std::map<std::string, CefRefPtr<CefBrowser>> sessions_;
  // Links opened one after another from the same tab line up below it in click order (Chrome):
  // the opener and the last tab opened from it.
  std::string link_opener_, link_last_;
  std::map<int, std::string> browser_nodes_;
  std::set<std::string> creating_;
  std::set<std::string> moving_unloads_;
  std::set<std::string> detached_removals_;
  std::map<std::string, std::function<void()>> tab_ready_;
  std::map<std::string, std::string> next_urls_;
  std::set<int> loaded_;
  std::set<int> crashed_;
  std::map<int, std::string> last_urls_;
  std::map<std::string, std::pair<bool, Reply>> closing_;
  std::set<std::string> archiving_;  // Archive commit in flight after the page closed.
  std::map<std::string,std::function<void(bool)>> auto_closing_;
  std::set<int> auto_archive_media_;
  std::vector<std::string> recent_;
  std::vector<std::string> external_pending_;
  bool external_opening_ = false;
  std::function<void(CefRefPtr<CefDictionaryValue>)> observer_;
  std::string error_, notice_;
  int notice_id_ = 0;
  double revision_ = 0;
  int new_tab_request_ = 0;
  int sidebar_request_ = 0;
  std::string sidebar_action_;
  int address_request_ = 0, opening_links_ = 0, pending_selections_ = 0;
  std::string selection_cursor_;
  std::string themed_space_;  // Space whose theme the shell shows.
  uint64_t theme_generation_=0;
  bool preserve_theme_preview_=false;
  std::string expanded_split_pane_;
  std::string link_peek_id_,link_peek_opener_,link_peek_url_;
  // Link sources: by page id once created ({url, initiator}), and by URL from
  // OpenPage until the page id exists.
  std::map<std::string, std::pair<std::string, std::string>> link_initiators_;
  std::map<std::string, std::string> link_initiator_by_url_;
  bool link_peek_closing_=false,link_peek_promoting_=false;
  // A Link Peek lives with its source tab: leaving the tab parks it (hidden,
  // page alive), returning shows it again. Source tab id -> parked preview.
  struct ParkedPeek { std::string id,url; int origin_x=0,origin_y=0; };
  std::map<std::string,ParkedPeek> parked_peeks_;
  std::set<std::string> closing_peeks_;  // Parked previews being closed.
  // Sidebar players: tabs shown (newest first), when each tab last played
  // sound, tabs dismissed until they play again, and each one's polled state.
  struct MediaState { bool has=false,paused=true,video=false; double time=0,duration=0; std::string title,artist; unsigned long long polled=0;
    CefRefPtr<CefRegistration> observer; int browser=-1,query=0; };
  std::vector<std::string> media_nodes_;
  std::map<std::string,unsigned long long> media_heard_,media_started_;
  std::set<std::string> media_dismissed_,media_audible_;
  std::map<std::string,MediaState> media_;
  // The page's Media Session as Chromium sees it (engine patch 09), by browser id: whether it
  // handles previous/next track, and its artwork (fetched by the page's own browser, as PNG).
  struct MediaSessionState { bool previous=false,next=false; std::string artwork_url,artwork; };
  std::map<int,MediaSessionState> media_sessions_;
  bool media_polling_=false;
  std::function<void(bool)> link_peek_after_;
  std::optional<std::pair<std::string,int>> split_preview_;
  std::map<std::string,std::string> split_focus_;
  unsigned split_resize_generation_ = 0;
  bool split_resizing_ = false;
  std::string split_resize_anchor_;
  int split_resize_initial_ = 5000;
  bool ready_ = false, stopped_ = false;
  bool startup_applied_ = false;
  bool shutting_down_ = false;
  int closing_commits_ = 0;
  int pending_commands_ = 0;
  std::map<std::string, std::string> saving_urls_;
  struct PageTitle {
    std::string url, text;
    bool operator==(const PageTitle&) const = default;
  };
  std::map<std::string, PageTitle> page_titles_;
  std::map<std::string, PageTitle> pending_titles_;  // Titles observed at saved destinations.
  std::set<std::string> saving_titles_;
};
}  // namespace red
