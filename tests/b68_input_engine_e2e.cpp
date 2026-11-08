// B6.9-I engine-path test for the legacy JoystickAxis / JoystickButton
// adapter overrides.
//
// The node-side mock E2E in b66 proves the node semantics; this test proves the
// real EngineInputAdapter path: a SDL3 *virtual* gamepad is attached (no
// hardware), DEV_Joystick::HandleEvents creates the real DEV_Joystick
// instance, and the adapter reads through DEV_Joystick::GetInstance's axis and
// button state. The adapter is the shipped class, compiled with the test hook
// only to bind it to the injected slot.
#include <cstdio>

#include <SDL3/SDL.h>

#include "DEV_Joystick.h"
#include "KX_GameAdapters.h"

KX_InputAdapter *KX_CreateEngineInputAdapterForTest(KX_Scene *scene, DEV_Joystick *joystick);

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const char *what, double actual = 0.0, double expected = 0.0)
{
  ++g_checks;
  std::printf("%s %s (actual=%g expected=%g)\n",
              ok ? "PASS" : "FAIL",
              what,
              actual,
              expected);
  if (!ok) {
    ++g_failures;
  }
}

}  // namespace

int main()
{
  if (!SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK)) {
    std::printf("FAIL SDL_Init: %s\n", SDL_GetError());
    return 1;
  }

  SDL_VirtualJoystickDesc desc;
  SDL_INIT_INTERFACE(&desc);
  desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  desc.naxes = JOYAXIS_MAX;
  desc.nbuttons = JOYBUT_MAX;
  desc.button_mask = 1u << 0;
  desc.axis_mask = 1u << 0;
  desc.name = "EBC engine adapter test pad";

  const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
  Check(id > 0, "SDL3 virtual gamepad attached");
  if (id == 0) {
    std::printf("FAIL SDL_AttachVirtualJoystick: %s\n", SDL_GetError());
    return 1;
  }

  /* A virtual joystick has no mapping of its own; register one for this
   * instance so SDL_IsGamepad / SDL_OpenGamepad succeed and the DEV_Joystick
   * device is created the same way it is for a real pad. */
  char guid_text[64];
  SDL_GUIDToString(SDL_GetJoystickGUIDForID(id), guid_text, sizeof(guid_text));
  char mapping[512];
  std::snprintf(mapping,
                sizeof(mapping),
                "%s,EBC Test Pad,a:b0,b:b1,x:b2,y:b3,back:b4,guide:b5,start:b6,"
                "leftstick:b7,rightstick:b8,leftshoulder:b9,rightshoulder:b10,"
                "dpup:b11,dpdown:b12,dpleft:b13,dpright:b14,"
                "leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:a4,"
                "righttrigger:a5,",
                guid_text);
  SDL_SetGamepadMapping(id, mapping);
  Check(SDL_IsGamepad(id), "virtual pad is a gamepad after mapping");
  if (!SDL_IsGamepad(id)) {
    std::printf("FAIL not a gamepad, mapping='%s': %s\n", mapping, SDL_GetError());
    return 1;
  }

  SDL_Joystick *virtual_joystick = SDL_OpenJoystick(id);
  Check(virtual_joystick != nullptr, "virtual joystick handle opened");
  if (virtual_joystick == nullptr) {
    std::printf("FAIL SDL_OpenJoystick: %s\n", SDL_GetError());
    return 1;
  }

  short addrem[JOYINDEX_MAX] = {0, 0, 0, 0, 0, 0, 0, 0};
  DEV_Joystick::HandleEvents(addrem);

  DEV_Joystick *joystick = DEV_Joystick::GetInstance(0);
  Check(joystick != nullptr, "DEV_Joystick::GetInstance(0) has the virtual pad");
  if (joystick == nullptr) {
    std::printf("FAIL no DEV_Joystick instance; cannot exercise the engine path\n");
    return 1;
  }

  SDL_SetJoystickVirtualAxis(virtual_joystick, 0, 16000);
  SDL_SetJoystickVirtualButton(virtual_joystick, 0, true);
  SDL_PumpEvents();
  DEV_Joystick::HandleEvents(addrem);

  KX_InputAdapter *adapter = KX_CreateEngineInputAdapterForTest(nullptr, joystick);
  Check(adapter != nullptr, "engine input adapter created");

  const float axis = adapter->JoystickAxis(0);
  Check(axis > 0.4f && axis < 0.6f,
        "engine adapter JoystickAxis(0) reads injected 16000/32767",
        axis,
        16000.0 / 32767.0);

  Check(adapter->JoystickButton(0, KX_InputMode::Hold),
        "engine adapter JoystickButton(0, Hold) true");
  Check(adapter->JoystickButton(0, KX_InputMode::Press),
        "engine adapter JoystickButton(0, Press) true on first frame");

  adapter->EndFrame();
  Check(!adapter->JoystickButton(0, KX_InputMode::Press),
        "Press is an edge, not a level");
  Check(adapter->JoystickButton(0, KX_InputMode::Hold),
        "Hold remains true after EndFrame");

  SDL_SetJoystickVirtualButton(virtual_joystick, 0, false);
  SDL_PumpEvents();
  DEV_Joystick::HandleEvents(addrem);
  Check(adapter->JoystickButton(0, KX_InputMode::Release),
        "engine adapter JoystickButton(0, Release) true after virtual release");
  Check(!adapter->JoystickButton(0, KX_InputMode::Hold),
        "Hold false after virtual release");

  delete adapter;
  SDL_CloseJoystick(virtual_joystick);
  SDL_DetachVirtualJoystick(id);
  SDL_Quit();
  std::printf("RESULT assertions=%d failures=%d\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
