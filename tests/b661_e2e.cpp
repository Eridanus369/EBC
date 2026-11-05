/* B6.6 headless harness: polar layout, circle knob, rich text parsing. */
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "KX_UIRenderer.h"
#include "KX_UIElement.h"

namespace {

int g_passed = 0;
int g_failed = 0;

void check(bool condition, const char *label, float actual, float expected)
{
  if (condition) {
    ++g_passed;
    std::printf("PASS %s (actual=%.4f expected=%.4f)\n", label, actual, expected);
  }
  else {
    ++g_failed;
    std::printf("FAIL %s (actual=%.4f expected=%.4f)\n", label, actual, expected);
  }
}

bool near(float actual, float expected, float tolerance = 1e-3f)
{
  return std::fabs(actual - expected) <= tolerance;
}

KX_UIElement MakeCanvas(int handle)
{
  KX_UIElement element;
  element.type = KX_UIElementType::Canvas;
  element.handle = handle;
  element.canvas = -1;
  return element;
}

KX_UIElement MakeLayout(int handle, int canvas, int layout_type, float radius, float start)
{
  KX_UIElement element;
  element.type = KX_UIElementType::Layout;
  element.handle = handle;
  element.canvas = canvas;
  element.layout_type = layout_type;
  element.radius = radius;
  element.starting_angle = start;
  element.position[0] = 0.0f;
  element.position[1] = 0.0f;
  element.size[0] = 0.0f;
  element.size[1] = 0.0f;
  return element;
}

KX_UIElement MakeChild(int handle, int canvas)
{
  KX_UIElement element;
  element.type = KX_UIElementType::Label;
  element.handle = handle;
  element.canvas = canvas;
  element.size[0] = 0.0f;
  element.size[1] = 0.0f;
  return element;
}

void ExpectRect(KX_UIRenderer &renderer, int handle, float x, float y, const char *label)
{
  float rect[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  const bool found = renderer.ResolvedRect(handle, rect);
  check(found && near(rect[0], x) && near(rect[1], y), label, rect[0], x);
}

/* --- 2.1 polar ----------------------------------------------------------- */
void TestPolar()
{
  std::vector<KX_UIElement> elements;
  elements.push_back(MakeCanvas(1));
  elements.push_back(MakeLayout(2, 1, KX_UI_LAYOUT_POLAR, 100.0f, 0.0f));
  for (int index = 0; index < 4; ++index) {
    elements.push_back(MakeChild(3 + index, 2));
  }
  /* KX_UIRenderer's destructor touches the GPU layer, so leak one instance. */
  KX_UIRenderer *renderer = new KX_UIRenderer();
  renderer->Draw(elements, 800, 600, false);
  ExpectRect(*renderer, 3, 100.0f, 0.0f, "polar 4 items slot 0 at (100, 0)");
  ExpectRect(*renderer, 4, 0.0f, 100.0f, "polar 4 items slot 1 at (0, 100)");
  ExpectRect(*renderer, 5, -100.0f, 0.0f, "polar 4 items slot 2 at (-100, 0)");
  ExpectRect(*renderer, 6, 0.0f, -100.0f, "polar 4 items slot 3 at (0, -100)");

  /* 8 items, 45 degree steps from 22.5 degrees. */
  std::vector<KX_UIElement> eight;
  eight.push_back(MakeCanvas(1));
  eight.push_back(MakeLayout(2, 1, KX_UI_LAYOUT_POLAR, 100.0f, 22.5f));
  for (int index = 0; index < 8; ++index) {
    eight.push_back(MakeChild(3 + index, 2));
  }
  KX_UIRenderer *renderer8 = new KX_UIRenderer();
  renderer8->Draw(eight, 800, 600, false);
  ExpectRect(*renderer8, 4, 100.0f * std::cos(67.5f * 3.14159265f / 180.0f),
             100.0f * std::sin(67.5f * 3.14159265f / 180.0f),
             "polar 8 items slot 1 at 67.5 degrees");
  ExpectRect(*renderer8, 5, 100.0f * std::cos(112.5f * 3.14159265f / 180.0f),
             100.0f * std::sin(112.5f * 3.14159265f / 180.0f),
             "polar 8 items slot 2 at 112.5 degrees");

  /* Single child with radius 0 must not divide by zero or crash. */
  std::vector<KX_UIElement> single;
  single.push_back(MakeCanvas(1));
  single.push_back(MakeLayout(2, 1, KX_UI_LAYOUT_POLAR, 0.0f, 0.0f));
  single.push_back(MakeChild(3, 2));
  KX_UIRenderer *renderer1 = new KX_UIRenderer();
  renderer1->Draw(single, 800, 600, false);
  ExpectRect(*renderer1, 3, 0.0f, 0.0f, "polar single child radius 0 stays at center");

  /* Static seam: the same math applied to a plain child list. */
  KX_UIElement a = MakeChild(11, 2);
  KX_UIElement b = MakeChild(12, 2);
  KX_UIElement c = MakeChild(13, 2);
  std::vector<KX_UIElement *> children{&a, &b, &c};
  const float center[2] = {0.0f, 0.0f};
  KX_UIRenderer::LayoutPolar(children, center, 50.0f, 90.0f);
  check(near(a.position[0], 0.0f) && near(a.position[1], 50.0f),
        "LayoutPolar 3 items slot 0 at 90 degrees",
        a.position[1],
        50.0f);
  check(near(b.position[0], 50.0f * std::cos(210.0f * 3.14159265f / 180.0f)) &&
            near(b.position[1], 50.0f * std::sin(210.0f * 3.14159265f / 180.0f)),
        "LayoutPolar 3 items slot 1 at 210 degrees",
        b.position[1],
        50.0f * std::sin(210.0f * 3.14159265f / 180.0f));
}

/* --- 2.2 circle knob ----------------------------------------------------- */
KX_UIElement MakeSlider(int handle, int canvas, float value)
{
  KX_UIElement element;
  element.type = KX_UIElementType::Slider;
  element.handle = handle;
  element.canvas = canvas;
  element.position[0] = 0.0f;
  element.position[1] = 0.0f;
  element.size[0] = 200.0f;
  element.size[1] = 40.0f;
  element.value = value;
  element.knob_size = 1.0f;
  element.knob_color[0] = 0.9f;
  element.knob_color[1] = 0.9f;
  element.knob_color[2] = 1.0f;
  element.knob_color[3] = 1.0f;
  return element;
}

void TestKnob()
{
  std::vector<KX_UIElement> one;
  one.push_back(MakeCanvas(1));
  one.push_back(MakeSlider(2, 1, 0.5f));
  KX_UIRenderer *renderer = new KX_UIRenderer();
  renderer->Draw(one, 800, 600, false);
  check(renderer->Stats().circle_calls == 1,
        "one visible slider issues exactly one DrawCircleApprox",
        float(renderer->Stats().circle_calls),
        1.0f);

  std::vector<KX_UIElement> two;
  two.push_back(MakeCanvas(1));
  two.push_back(MakeSlider(2, 1, 0.0f));
  two.push_back(MakeSlider(3, 1, 1.0f));
  KX_UIRenderer *renderer_two = new KX_UIRenderer();
  renderer_two->Draw(two, 800, 600, false);
  check(renderer_two->Stats().circle_calls == 2,
        "two visible sliders issue two DrawCircleApprox",
        float(renderer_two->Stats().circle_calls),
        2.0f);

  std::vector<KX_UIElement> hidden;
  hidden.push_back(MakeCanvas(1));
  KX_UIElement slider = MakeSlider(2, 1, 0.5f);
  slider.visible = false;
  hidden.push_back(slider);
  KX_UIRenderer *renderer_hidden = new KX_UIRenderer();
  renderer_hidden->Draw(hidden, 800, 600, false);
  check(renderer_hidden->Stats().circle_calls == 0,
        "hidden slider issues no DrawCircleApprox",
        float(renderer_hidden->Stats().circle_calls),
        0.0f);
}

/* --- 2.3 rich text ------------------------------------------------------- */
bool SegmentIs(const std::vector<KX_UIRichSegment> &segments,
               size_t index,
               const std::string &text,
               float r,
               float g,
               float b)
{
  if (index >= segments.size()) {
    return false;
  }
  const KX_UIRichSegment &segment = segments[index];
  return segment.text == text && near(segment.color[0], r) && near(segment.color[1], g) &&
         near(segment.color[2], b);
}

void TestRichText()
{
  const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};

  std::vector<KX_UIRichSegment> plain = KX_UIRenderer::ParseRichText("plain", white);
  check(plain.size() == 1 && SegmentIs(plain, 0, "plain", 1.0f, 1.0f, 1.0f),
        "plain text is one base-color run",
        float(plain.size()),
        1.0f);

  std::vector<KX_UIRichSegment> named =
      KX_UIRenderer::ParseRichText("Hello {color:red}World{/color}", white);
  check(named.size() == 2 && SegmentIs(named, 0, "Hello ", 1.0f, 1.0f, 1.0f) &&
            SegmentIs(named, 1, "World", 1.0f, 0.0f, 0.0f),
        "named red colors only World",
        float(named.size()),
        2.0f);

  std::vector<KX_UIRichSegment> hex =
      KX_UIRenderer::ParseRichText("A {color:#00FF00}B{/color} C", white);
  check(hex.size() == 3 && SegmentIs(hex, 0, "A ", 1.0f, 1.0f, 1.0f) &&
            SegmentIs(hex, 1, "B", 0.0f, 1.0f, 0.0f) &&
            SegmentIs(hex, 2, " C", 1.0f, 1.0f, 1.0f),
        "hex green colors only B",
        float(hex.size()),
        3.0f);

  std::vector<KX_UIRichSegment> nested =
      KX_UIRenderer::ParseRichText("x {color:red}a{color:blue}b{/color}c{/color}", white);
  check(SegmentIs(nested, 2, "b", 0.0f, 0.0f, 1.0f),
        "nested markup lets the innermost color win",
        nested.size() > 2 ? nested[2].color[2] : -1.0f,
        1.0f);
  check(KX_UIRenderer::RichTextNestedWarnings() == 1,
        "nested markup warns once",
        float(KX_UIRenderer::RichTextNestedWarnings()),
        1.0f);

  std::vector<KX_UIRichSegment> nested_again =
      KX_UIRenderer::ParseRichText("{color:red}a{color:green}b{/color}{/color}", white);
  (void)nested_again;
  check(KX_UIRenderer::RichTextNestedWarnings() == 1,
        "second nested text does not warn again",
        float(KX_UIRenderer::RichTextNestedWarnings()),
        1.0f);

  std::vector<KX_UIRichSegment> unknown =
      KX_UIRenderer::ParseRichText("{color:notacolor}x{/color}", white);
  check(unknown.size() == 1 && unknown[0].text == "{color:notacolor}x",
        "unknown color name keeps the literal markup",
        float(unknown.size()),
        1.0f);
}

}  // namespace

int main()
{
  TestPolar();
  TestKnob();
  TestRichText();
  std::printf("B6.6 harness: %d PASS, %d FAIL\n", g_passed, g_failed);
  return g_failed == 0 ? 0 : 1;
}
