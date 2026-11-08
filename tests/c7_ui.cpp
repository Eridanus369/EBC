/* B6.7 headless harness: multi font LoadFont/ResolveFont, basic vertical
 * text and the Polar hit-test write-back. No GPU and no bpy. */
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "KX_GameAdapters.h"
#include "KX_UIRenderer.h"
#include "KX_UIElement.h"

namespace {

int g_passed = 0;
int g_failed = 0;

void check(bool condition, const char *label)
{
  if (condition) {
    ++g_passed;
    std::printf("PASS %s\n", label);
  }
  else {
    ++g_failed;
    std::printf("FAIL %s\n", label);
  }
}

void check_text(bool condition, const char *label, const std::string &actual, const std::string &expected)
{
  if (condition) {
    ++g_passed;
    std::printf("PASS %s (actual='%s')\n", label, actual.c_str());
  }
  else {
    ++g_failed;
    std::printf("FAIL %s (actual='%s' expected='%s')\n", label, actual.c_str(), expected.c_str());
  }
}

void check_number(bool condition, const char *label, float actual, float expected)
{
  if (condition) {
    ++g_passed;
    std::printf("PASS %s (actual=%.3f expected=%.3f)\n", label, actual, expected);
  }
  else {
    ++g_failed;
    std::printf("FAIL %s (actual=%.3f expected=%.3f)\n", label, actual, expected);
  }
}

bool near(float actual, float expected, float tolerance = 1e-3f)
{
  return std::fabs(actual - expected) <= tolerance;
}

/* --- C.7.1 font registry -------------------------------------------------- */

void TestFontRegistry()
{
  /* Leaked on purpose: the renderer destructor releases GPU resources, which
   * needs a live context the headless harness does not have. */
  KX_UIRenderer *renderer = new KX_UIRenderer();

  /* Empty arguments are rejected without touching BLF. */
  check(!renderer->LoadFont("", "/datafiles/fonts/NotoSans.ttf"), "LoadFont rejects an empty name");
  check(!renderer->LoadFont("NotoSans", ""), "LoadFont rejects an empty path");
  /* A missing file must fail cleanly, not abort the process. */
  check(!renderer->LoadFont("GhostFace", "/nonexistent/ebc/ghost.ttf"),
        "LoadFont reports a missing font file");

  /* Simulated successful loads: the harness has no FreeType context, so the
   * ids are registered the way BLF_load_unique would return them. */
  KX_UIRenderer::RegisterFont("NotoSans", 7);
  KX_UIRenderer::RegisterFont("NotoMono", 9);
  check_number(KX_UIRenderer::ResolveFont("NotoSans") == 7,
               "ResolveFont returns the NotoSans id",
               float(KX_UIRenderer::ResolveFont("NotoSans")),
               7.0f);
  check_number(KX_UIRenderer::ResolveFont("NotoMono") == 9,
               "ResolveFont returns the NotoMono id",
               float(KX_UIRenderer::ResolveFont("NotoMono")),
               9.0f);
  /* An empty name means "default face", no warning. */
  check_number(KX_UIRenderer::ResolveFont("") == 0, "ResolveFont maps the empty name to the default face",
               float(KX_UIRenderer::ResolveFont("")), 0.0f);
  check_number(KX_UIRenderer::MissingFontWarnings() == 0,
               "an empty font name does not warn", float(KX_UIRenderer::MissingFontWarnings()), 0.0f);

  /* A legacy datablock name that was never loaded falls back to the default
   * face and warns exactly once. */
  check_number(KX_UIRenderer::ResolveFont("LegacyUIFont.001") == 0,
               "unknown font falls back to the default face",
               float(KX_UIRenderer::ResolveFont("LegacyUIFont.001")),
               0.0f);
  check_number(KX_UIRenderer::MissingFontWarnings() == 1,
               "missing font warning is emitted once",
               float(KX_UIRenderer::MissingFontWarnings()),
               1.0f);
  check_number(KX_UIRenderer::ResolveFont("AnotherOldFace") == 0,
               "a second unknown font also falls back",
               float(KX_UIRenderer::ResolveFont("AnotherOldFace")),
               0.0f);
  check_number(KX_UIRenderer::MissingFontWarnings() == 1,
               "missing font warning stays at one",
               float(KX_UIRenderer::MissingFontWarnings()),
               1.0f);
}

/* --- C.7.1 labels bound to a font name ------------------------------------ */

void TestLabelFontBinding()
{
  KX_MockUIAdapter adapter;
  const int canvas = adapter.CreateCanvas("hud");

  check(adapter.LoadFont("NotoSans", "/datafiles/fonts/NotoSans.ttf"),
        "mock LoadFont accepts a name and path");
  check(adapter.load_font_calls == 1, "mock LoadFont counted once");
  check_text(adapter.fonts.count("NotoSans") == 1, "mock LoadFont stores the path",
             adapter.fonts.count("NotoSans") ? adapter.fonts["NotoSans"] : std::string(),
             "/datafiles/fonts/NotoSans.ttf");
  check(!adapter.LoadFont("", "/datafiles/fonts/NotoSans.ttf"), "mock LoadFont rejects an empty name");
  check(!adapter.LoadFont("NotoSans", ""), "mock LoadFont rejects an empty path");

  const int label_a = adapter.CreateLabel(canvas, "Hello");
  const int label_b = adapter.CreateLabel(canvas, "World");
  check(adapter.SetAttribute(label_a, "font", KX_RuntimeValue::String("NotoSans")),
        "label A accepts a font name");
  check(adapter.SetAttribute(label_b, "font", KX_RuntimeValue::String("NotoMono")),
        "label B accepts a different font name");

  const KX_UIElement *element_a = adapter.Find(label_a);
  const KX_UIElement *element_b = adapter.Find(label_b);
  check_text(element_a != nullptr && element_a->font == "NotoSans",
             "label A is bound to NotoSans",
             element_a != nullptr ? element_a->font : std::string(), "NotoSans");
  check_text(element_b != nullptr && element_b->font == "NotoMono",
             "label B is bound to NotoMono",
             element_b != nullptr ? element_b->font : std::string(), "NotoMono");
  check(element_a != nullptr && element_b != nullptr &&
            element_a->handle != element_b->handle,
        "the two labels are distinct elements");

  /* G.1: the two Font port values must resolve to two independent BLF slots,
   * so each label is drawn with its own face instead of silently sharing
   * the default font. */
  check(element_a != nullptr && element_b != nullptr &&
            KX_UIRenderer::ResolveFont(element_a->font) == 7 &&
            KX_UIRenderer::ResolveFont(element_b->font) == 9 &&
            KX_UIRenderer::ResolveFont(element_a->font) !=
                KX_UIRenderer::ResolveFont(element_b->font),
        "the two labels resolve to distinct BLF font ids");

  /* A label carrying an unknown (legacy datablock) name still reaches the
   * fallback branch instead of crashing. */
  const int label_c = adapter.CreateLabel(canvas, "Legacy");
  check(adapter.SetAttribute(label_c, "font", KX_RuntimeValue::String("LegacyUIFont.001")),
        "label C accepts a legacy datablock name");
  check_number(KX_UIRenderer::ResolveFont("LegacyUIFont.001") == 0,
               "legacy font name resolves to the default face",
               float(KX_UIRenderer::ResolveFont("LegacyUIFont.001")), 0.0f);
}

/* --- C.7.2 basic vertical text -------------------------------------------- */

KX_UIElement MakeCanvas(int handle)
{
  KX_UIElement element;
  element.type = KX_UIElementType::Canvas;
  element.handle = handle;
  element.canvas = -1;
  element.size[0] = 640.0f;
  element.size[1] = 480.0f;
  return element;
}

KX_UIElement MakeLabel(int handle,
                       int canvas,
                       const std::string &text,
                       float font_size,
                       bool vertical)
{
  KX_UIElement element;
  element.type = KX_UIElementType::Label;
  element.handle = handle;
  element.canvas = canvas;
  element.text = text;
  element.font_size = font_size;
  element.vertical = vertical;
  element.position[0] = 10.0f;
  element.position[1] = 20.0f;
  element.size[0] = 120.0f;
  element.size[1] = 100.0f;
  return element;
}

void TestVerticalText()
{
  KX_UIElement probe;
  probe.vertical = false;
  KX_RuntimeValue value = KX_RuntimeValue::Bool(true);
  check(KX_UISetElementAttribute(probe, "vertical", value), "vertical attribute is accepted");
  check(probe.vertical, "vertical attribute stores true");
  check(probe.revision == 1, "vertical attribute bumps the revision");
  KX_RuntimeValue read_back;
  check(KX_UIGetElementAttribute(probe, "vertical", read_back), "vertical attribute can be read back");
  check(read_back.AsBool(), "vertical attribute reads back true");
  check(KX_UISetElementAttribute(probe, "vertical", KX_RuntimeValue::Bool(false)),
        "vertical attribute can be turned off");
  check(!probe.vertical, "vertical attribute stores false");

  std::vector<KX_UIElement> elements;
  elements.push_back(MakeCanvas(1));
  const std::string text = "你好AB";
  elements.push_back(MakeLabel(2, 1, text, 16.0f, true));
  /* Same text without vertical, used as the horizontal control. */
  elements.push_back(MakeLabel(3, 1, text, 16.0f, false));
  /* G.2: more than four characters still break one character per row. */
  elements.push_back(MakeLabel(4, 1, "你好ABC", 16.0f, true));

  KX_UIRenderer *renderer = new KX_UIRenderer();
  renderer->Draw(elements, 640, 480, false);

  std::vector<std::string> lines;
  std::vector<float> heights;
  check(renderer->LaidOutLines(2, &lines, &heights), "vertical label laid out");
  check(lines.size() == 4, "vertical label has one row per UTF-8 character");
  check_text(lines.size() == 4 && lines[0] == "你", "vertical row 0 is the first CJK character",
             lines.empty() ? std::string() : lines[0], "你");
  check_text(lines.size() == 4 && lines[1] == "好", "vertical row 1 is the second CJK character",
             lines.size() < 2 ? std::string() : lines[1], "好");
  check_text(lines.size() == 4 && lines[2] == "A", "vertical row 2 is the first Latin character",
             lines.size() < 3 ? std::string() : lines[2], "A");
  check_text(lines.size() == 4 && lines[3] == "B", "vertical row 3 is the second Latin character",
             lines.size() < 4 ? std::string() : lines[3], "B");

  bool heights_ok = heights.size() == 4;
  for (float height : heights) {
    heights_ok = heights_ok && near(height, 16.0f);
  }
  check(heights_ok, "vertical row height equals the font size");

  /* The cursor advances one font size per character, so the per-character Y
   * coordinates are strictly increasing. */
  float cursor_y = 0.0f;
  bool y_increasing = true;
  for (size_t index = 0; index < heights.size(); ++index) {
    if (!(cursor_y > 0.0f || index == 0)) {
      y_increasing = false;
    }
    cursor_y += heights[index];
  }
  check(y_increasing, "vertical character Y coordinates increase per row");
  check_number(near(cursor_y, 64.0f), "vertical total height is characters x row height",
               cursor_y, 64.0f);

  /* Horizontal control: one row, height = font size * line height. */
  check(renderer->LaidOutLines(3, &lines, &heights), "horizontal label laid out");
  check(lines.size() == 1, "horizontal label keeps one row");
  check(heights.size() == 1 && near(heights[0], 16.0f * 1.5f),
        "horizontal row height keeps the line spacing");

  /* G.2: five-character vertical label -> five rows, same row height. */
  check(renderer->LaidOutLines(4, &lines, &heights), "five-character vertical label laid out");
  check(lines.size() == 5, "five-character vertical label has five rows");
  check_text(lines.size() == 5 && lines[4] == "C",
             "the fifth row is the fifth UTF-8 character",
             lines.size() < 5 ? std::string() : lines[4], "C");
  bool five_heights_ok = heights.size() == 5;
  for (float height : heights) {
    five_heights_ok = five_heights_ok && near(height, 16.0f);
  }
  check(five_heights_ok, "five-character vertical rows keep the font-size height");
}

/* --- Polar hit-test write-back -------------------------------------------- */

void TestPolarHitTest()
{
  KX_MockUIAdapter adapter;
  const int canvas = adapter.CreateCanvas("screen");
  const int layout = adapter.CreateLayout(canvas, KX_UI_LAYOUT_POLAR);
  check(adapter.SetAttribute(layout, "position", KX_RuntimeValue::Vector(200.0f, 200.0f, 0.0f)),
        "polar layout accepts a position");
  check(adapter.SetAttribute(layout, "size", KX_RuntimeValue::Vector(100.0f, 100.0f, 0.0f)),
        "polar layout accepts a size");
  check(adapter.SetAttribute(layout, "radius", KX_RuntimeValue::Float(100.0f)),
        "polar layout accepts a radius");
  check(adapter.SetAttribute(layout, "starting_angle", KX_RuntimeValue::Float(0.0f)),
        "polar layout accepts a starting angle");

  std::vector<int> children;
  for (int index = 0; index < 4; ++index) {
    const int child = adapter.CreateLabel(layout, "item");
    adapter.SetAttribute(child, "size", KX_RuntimeValue::Vector(20.0f, 20.0f, 0.0f));
    children.push_back(child);
  }

  /* Before the frame the children still carry the authored position, which is
   * the old divergence between Draw and HitTest. */
  check(near(adapter.Find(children[0])->position[0], 0.0f) &&
            near(adapter.Find(children[0])->position[1], 0.0f),
        "polar child starts at the authored position");

  adapter.BeginFrame(0.016f, 350.0f, 250.0f, false);

  const bool slots_ok =
      near(adapter.Find(children[0])->position[0], 350.0f) &&
      near(adapter.Find(children[0])->position[1], 250.0f) &&
      near(adapter.Find(children[1])->position[0], 250.0f) &&
      near(adapter.Find(children[1])->position[1], 350.0f) &&
      near(adapter.Find(children[2])->position[0], 150.0f) &&
      near(adapter.Find(children[2])->position[1], 250.0f) &&
      near(adapter.Find(children[3])->position[0], 250.0f) &&
      near(adapter.Find(children[3])->position[1], 150.0f);
  check(slots_ok, "layout writes the four polar slots back onto the children");

  check(adapter.hovered_handle == children[0],
        "hit test at the first polar slot picks the first child");
  check(adapter.HitTestPoint(250.0f, 350.0f) == children[1],
        "hit test at the second polar slot picks the second child");
  check(adapter.HitTestPoint(150.0f, 250.0f) == children[2],
        "hit test at the third polar slot picks the third child");
  check(adapter.HitTestPoint(250.0f, 150.0f) == children[3],
        "hit test at the fourth polar slot picks the fourth child");
  /* The authored position (0,0) is no longer a hit, and the empty centre is
   * not a child either. */
  check(adapter.HitTestPoint(0.0f, 0.0f) == -1,
        "the authored position no longer hits a polar child");
  check(adapter.HitTestPoint(250.0f, 250.0f) != children[0] &&
            adapter.HitTestPoint(250.0f, 250.0f) != children[1] &&
            adapter.HitTestPoint(250.0f, 250.0f) != children[2] &&
            adapter.HitTestPoint(250.0f, 250.0f) != children[3],
        "the polar centre does not hit a child");

  /* Calling BeginFrame again must not accumulate an offset. */
  const float first_x = adapter.Find(children[0])->position[0];
  adapter.BeginFrame(0.016f, 350.0f, 250.0f, false);
  check(near(adapter.Find(children[0])->position[0], first_x),
        "a second frame keeps the same polar slot");
}

}  // namespace

int main()
{
  TestFontRegistry();
  TestLabelFontBinding();
  TestVerticalText();
  TestPolarHitTest();

  std::printf("\nSUMMARY passed=%d failed=%d\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
