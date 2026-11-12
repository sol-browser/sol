#include "windows/welcome_music.h"
#include <windows.h>
#include <mfplay.h>
#include <wrl/client.h>
#include <string>
#include "include/base/cef_callback.h"
#include "include/base/cef_logging.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_closure_task.h"

namespace red {
namespace {
// Media Foundation's simple player on the UI thread (an STA with a message loop,
// which MFPlay uses for its own events). Never destroyed at exit: the process
// ending closes the audio.
Microsoft::WRL::ComPtr<IMFPMediaPlayer>& Player() {
  static auto* player = new Microsoft::WRL::ComPtr<IMFPMediaPlayer>;
  return *player;
}
// The welcome's music plays softly, under the picture.
constexpr float kVolume = 0.4f;
bool muted_ = false;
int fade_ = 0;  // Bumped by every Play/Stop; a fade step from an older one stops.

void Close() {
  if (auto& player = Player()) { player->Shutdown(); player.Reset(); }
}

void FadeStep(int fade, float volume) {
  auto& player = Player();
  if (fade != fade_ || !player) return;
  if (volume <= 0) { Close(); return; }
  player->SetVolume(volume);
  CefPostDelayedTask(TID_UI, base::BindOnce(&FadeStep, fade, volume - kVolume / 20), 50);
}
}  // namespace

std::filesystem::path WelcomeMusic::Track() {
  std::error_code error;
  wchar_t executable[MAX_PATH];
  const DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
  if (length && length < MAX_PATH) {
    const auto shipped = std::filesystem::path(executable).parent_path() / L"welcome.mp3";
    if (std::filesystem::is_regular_file(shipped, error)) return shipped;
  }
  wchar_t value[MAX_PATH]{};
  const DWORD size = GetEnvironmentVariableW(L"SOL_WELCOME_MUSIC", value, MAX_PATH);
  if (size && size < MAX_PATH) {
    const std::filesystem::path test(value);
    if (test.is_absolute() && test.extension() == L".mp3" && std::filesystem::is_regular_file(test, error)) return test;
  }
  return {};
}

void WelcomeMusic::Play() {
  ++fade_;
  auto& player = Player();
  Close();  // Playing again starts the track over (the welcome's replay).
  const auto track = Track();
  if (track.empty()) return;
  const HRESULT result = MFPCreateMediaPlayer(track.c_str(), TRUE, MFP_OPTION_NONE, nullptr, nullptr, &player);
  if (FAILED(result)) {
    LOG(WARNING) << "Welcome music could not start (0x" << std::hex << result << ")";
    player.Reset();
    return;
  }
  player->SetVolume(kVolume);  // MFPlay keeps it until the track starts.
  player->SetMute(muted_);
}

void WelcomeMusic::SetMuted(bool muted) {
  muted_ = muted;
  if (auto& player = Player()) player->SetMute(muted);
}

void WelcomeMusic::Stop() {
  if (!Player()) return;
  FadeStep(++fade_, kVolume * 0.95f);
}
}  // namespace red
