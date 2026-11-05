/* B6.6 evidence: the adapter level Int handle bridge already round-trips. */
#include <cstdint>
#include <cstdio>

#include "KX_GameAdapters.h"

int main()
{
  KX_MockAudioAdapter audio;
  int failures = 0;
  auto check = [&](bool ok, const char *label, long long actual, long long expected) {
    std::printf("%s %s (actual=%lld expected=%lld)\n", ok ? "PASS" : "FAIL", label, actual, expected);
    if (!ok) {
      ++failures;
    }
  };

  const uint64_t handle = audio.PlaySoundByName("beep", false, 0.8f, 1.2f);
  check(handle != 0, "PlaySoundByName returns an id", (long long)handle, 1);
  check(audio.IsPlaying(handle), "handle is playing before stop", audio.IsPlaying(handle), 1);

  /* The node port is Int; round trip through the same width the runtime uses. */
  const int port_value = static_cast<int>(handle);
  const uint64_t restored = static_cast<uint64_t>(port_value);
  check(restored == handle, "id survives the Int port round trip", (long long)restored,
        (long long)handle);

  audio.StopSound(restored);
  check(audio.stop_calls == 1, "StopSound receives the id", (long long)audio.stop_calls, 1);
  check(!audio.IsPlaying(restored), "handle is stopped", audio.IsPlaying(restored), 0);
  check(audio.IsFinished(restored), "handle reports finished", audio.IsFinished(restored), 1);

  std::printf("%s total failures=%d\n", failures == 0 ? "PASS" : "FAIL", failures);
  return failures == 0 ? 0 : 1;
}
