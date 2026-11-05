#pragma once
#include <filesystem>

namespace red {
// The welcome's music, played by Windows (MCI) so it starts with the welcome:
// Chromium's autoplay rules would hold page audio until a click. The track is
// welcome.mp3 beside sol.exe; SOL_WELCOME_MUSIC may name another file for
// testing. Sol ships without one until the music is licensed (the welcome is
// then silent). UI thread only.
class WelcomeMusic {
 public:
  static std::filesystem::path Track();
  static bool Available() { return !Track().empty(); }
  static void Play();
  static void SetMuted(bool muted);
  // Fades out over about a second, then closes the track.
  static void Stop();
};
}  // namespace red
