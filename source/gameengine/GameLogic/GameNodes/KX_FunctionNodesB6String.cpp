/*
 * B6.2 advanced string and list node family.
 *
 * Lists stay inside KX_ValuePool, following the B2/B3 ownership rules: every
 * produced list is a fresh pool allocation that survives the current frame and
 * the next one.
 *
 * ListFilter / ListMap accept a deliberately small arithmetic expression
 * language (documented in docs/B6_STRING_LIST.md); anything outside it is
 * rejected with an explicit diagnostic instead of being silently ignored.
 */
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <regex>
#include <string>
#include <vector>

#include "KX_ExecutionContext.h"
#include "KX_FunctionNode.h"
#include "KX_GameAdapters.h"
#include "KX_GameObject.h"
#include "KX_NodeRegistry.h"
#include "KX_PropertyAccessor.h"

#define B6S_ARRAY_LEN(array) (sizeof(array) / sizeof(array[0]))
#define FP(name, type) \
  { name, type, false }
#define B6S_COUNTS(array) static_cast<uint32_t>(B6S_ARRAY_LEN(array))

#define B6S_VALUE(id, ins, outs, exec) \
  { id, ins, B6S_COUNTS(ins), outs, B6S_COUNTS(outs), exec, nullptr, nullptr, 0, \
    KX_RUNTIME_DATA_ALIGNMENT, true, false, false }

#define B6S_VALUE_STATE(id, ins, outs, exec, state_type) \
  { id, ins, B6S_COUNTS(ins), outs, B6S_COUNTS(outs), exec, nullptr, nullptr, \
    static_cast<uint32_t>(sizeof(state_type)), static_cast<uint32_t>(alignof(state_type)), \
    true, false, false }

namespace {

struct B6SWarnState {
  bool warned = false;
};

const size_t kMaxElements = KX_ValuePool::kMaxListElements;

/* std::regex has no timeout parameter. Input length is capped instead so a
 * pathological pattern is bounded by a fixed amount of text; the residual
 * backtracking cost is documented in docs/B6_STRING_LIST.md. */
const size_t kMaxRegexSource = 4096;
const size_t kMaxRegexPattern = 512;

/* ---- regex safety guard ------------------------------------------------- */

/* Accepts "{n}", "{n,}" and "{n,m}" only; a bare "{" is an ordinary literal in
 * ECMAScript. */
struct RegexBraceQuantifier {
  size_t close = 0;
  bool min_zero = false;
  bool unbounded = false;
};

bool IsBraceQuantifier(const std::string &pattern, size_t open,
                       RegexBraceQuantifier &quantifier)
{
  const size_t size = pattern.size();
  size_t index = open + 1;
  size_t digits = 0;
  unsigned long minimum = 0;
  while (index < size && std::isdigit(static_cast<unsigned char>(pattern[index]))) {
    if (minimum < 4096) {
      minimum = minimum * 10 + static_cast<unsigned long>(pattern[index] - '0');
    }
    ++index;
    ++digits;
  }
  if (digits == 0) {
    return false;
  }
  bool unbounded = false;
  if (index < size && pattern[index] == ',') {
    ++index;
    const size_t maximum_begin = index;
    while (index < size && std::isdigit(static_cast<unsigned char>(pattern[index]))) {
      ++index;
    }
    unbounded = index == maximum_begin;
  }
  if (index >= size || pattern[index] != '}') {
    return false;
  }
  quantifier.close = index;
  quantifier.min_zero = minimum == 0;
  quantifier.unbounded = unbounded;
  return true;
}

/* One bracket-depth level of the guard scan. `atoms` counts the atoms of the
 * current alternative (a group counts as one atom); `last_*` describes the
 * final atom, and `required_before_last` records whether an earlier atom in the
 * same alternative still needs at least one character. */
struct RegexGroupFrame {
  size_t atoms = 0;
  bool required_before_last = false;
  bool last_nullable = true;
  bool last_ends_unbounded = false;
  bool any_branch_nullable = false;
  bool has_alternation = false;
};

bool BranchNullable(const RegexGroupFrame &frame)
{
  return !frame.required_before_last && frame.last_nullable;
}

void PushRegexAtom(std::vector<RegexGroupFrame> &groups, bool nullable,
                   bool ends_unbounded)
{
  if (groups.empty()) {
    return;
  }
  RegexGroupFrame &frame = groups.back();
  if (frame.atoms > 0) {
    frame.required_before_last = frame.required_before_last || !frame.last_nullable;
  }
  ++frame.atoms;
  frame.last_nullable = nullable;
  frame.last_ends_unbounded = ends_unbounded;
}

/* Rejects, before std::regex is constructed, the quantified groups that make
 * the engine backtrack catastrophically. A group followed by a quantifier is
 * rejected when either
 *   1) the whole body is one unbounded repetition ((a+)+, (a*)*, (.*)*,
 *      ((a+))+), or
 *   2) the body can match the empty string ((a?)+, (a?)*).
 * Sequences such as (\d+,)+ and (ab?)+ stay allowed: their body is a
 * concatenation, not a bare repetition. Consecutive quantifiers (a**, a++,
 * a?*, a*+) are rejected outright. Lazy suffixes (a+?, a*?, a??) count as one
 * quantifier. One pass, one bracket-depth stack; std::regex is never asked to
 * decide this. */
bool IsSafeRegexPattern(const std::string &pattern)
{
  std::vector<RegexGroupFrame> groups;
  bool previous_quantifier = false;
  bool previous_lazy = false;
  bool previous_closed_group = false;
  bool closed_group_nullable = false;
  bool closed_group_pure_unbounded = false;
  bool in_class = false;
  bool escaped = false;

  for (size_t index = 0; index < pattern.size(); ++index) {
    const char current = pattern[index];

    if (escaped) {
      escaped = false;
      /* "\b" and "\B" are zero-width assertions, not consuming atoms. */
      if (current != 'b' && current != 'B') {
        PushRegexAtom(groups, false, false);
      }
      previous_quantifier = false;
      previous_lazy = false;
      previous_closed_group = false;
      continue;
    }
    if (in_class) {
      if (current == '\\') {
        escaped = true;
      }
      else if (current == ']') {
        in_class = false;
        previous_quantifier = false;
        previous_lazy = false;
        previous_closed_group = false;
      }
      continue;
    }
    if (current == '\\') {
      escaped = true;
      previous_quantifier = false;
      previous_lazy = false;
      previous_closed_group = false;
      continue;
    }
    if (current == '[') {
      in_class = true;
      PushRegexAtom(groups, false, false);
      previous_quantifier = false;
      previous_lazy = false;
      previous_closed_group = false;
      continue;
    }
    if (current == '(') {
      groups.push_back(RegexGroupFrame());
      /* "(?" introduces a non-capturing or lookaround group, not a quantifier. */
      if (index + 1 < pattern.size() && pattern[index + 1] == '?') {
        ++index;
      }
      previous_quantifier = false;
      previous_lazy = false;
      previous_closed_group = false;
      continue;
    }
    if (current == ')') {
      RegexGroupFrame frame;
      if (!groups.empty()) {
        frame = groups.back();
        groups.pop_back();
      }
      const bool body_nullable =
          frame.any_branch_nullable || BranchNullable(frame);
      const bool body_pure_unbounded =
          !frame.has_alternation && frame.atoms == 1 && frame.last_ends_unbounded;
      /* The closed group is one atom of the enclosing alternative; its
       * unbounded tail propagates so ((a+))+ is caught one level up. */
      PushRegexAtom(groups, body_nullable, frame.last_ends_unbounded);
      closed_group_nullable = body_nullable;
      closed_group_pure_unbounded = body_pure_unbounded;
      previous_quantifier = false;
      previous_lazy = false;
      previous_closed_group = true;
      continue;
    }
    if (current == '|') {
      if (!groups.empty()) {
        RegexGroupFrame &frame = groups.back();
        frame.has_alternation = true;
        frame.any_branch_nullable =
            frame.any_branch_nullable || BranchNullable(frame);
        frame.atoms = 0;
        frame.required_before_last = false;
        frame.last_nullable = true;
        frame.last_ends_unbounded = false;
      }
      previous_quantifier = false;
      previous_lazy = false;
      previous_closed_group = false;
      continue;
    }
    if (current == '^' || current == '$') {
      /* Zero-width anchors do not consume characters. */
      previous_quantifier = false;
      previous_lazy = false;
      previous_closed_group = false;
      continue;
    }

    bool is_quantifier = current == '*' || current == '+' || current == '?';
    RegexBraceQuantifier brace;
    const bool brace_quantifier =
        current == '{' && IsBraceQuantifier(pattern, index, brace);
    if (brace_quantifier) {
      is_quantifier = true;
    }
    if (is_quantifier) {
      if (previous_quantifier) {
        if (current == '?' && !previous_lazy) {
          previous_lazy = true;
          previous_closed_group = false;
          continue;
        }
        return false; /* a** / a++ / a?* / a*+ */
      }
      if (previous_closed_group &&
          (closed_group_nullable || closed_group_pure_unbounded)) {
        return false; /* (a+)+ / (a?)+ / (.*)* / ((a+))+ */
      }
      bool min_zero = current == '*' || current == '?';
      bool unbounded = current == '*' || current == '+';
      if (brace_quantifier) {
        min_zero = brace.min_zero;
        unbounded = brace.unbounded;
      }
      if (!groups.empty() && groups.back().atoms > 0) {
        RegexGroupFrame &frame = groups.back();
        frame.last_nullable = frame.last_nullable || min_zero;
        frame.last_ends_unbounded = unbounded;
      }
      previous_quantifier = true;
      previous_lazy = false;
      previous_closed_group = false;
      if (brace_quantifier) {
        index = brace.close;
      }
      continue;
    }

    /* Ordinary literal atom, including a lone "{" or "}" or ".". */
    PushRegexAtom(groups, false, false);
    previous_quantifier = false;
    previous_lazy = false;
    previous_closed_group = false;
  }
  return true;
}

/* ------------------------------------------------------------------ helpers */

std::string InString(KX_FunctionNode &node, KX_ExecutionContext &ctx, uint32_t index)
{
  const char *text = node.In(ctx, index).AsString();
  return text != nullptr ? std::string(text) : std::string();
}

KX_ValueList *InList(KX_FunctionNode &node, KX_ExecutionContext &ctx, uint32_t index)
{
  return static_cast<KX_ValueList *>(node.In(ctx, index).AsList());
}

KX_ValueList &CopyIntoPool(KX_ExecutionContext &ctx, const KX_ValueList *source, size_t extra)
{
  KX_ValueList &destination = ctx.Pool().AllocList(
      (source != nullptr ? source->size() : 0) + extra);
  if (source != nullptr) {
    destination.assign(source->begin(), source->end());
  }
  return destination;
}

std::string ElementText(const KX_RuntimeValue &value)
{
  switch (value.type) {
    case KX_VALUE_STRING: {
      const char *text = value.AsString();
      return text != nullptr ? std::string(text) : std::string();
    }
    case KX_VALUE_INT:
      return std::to_string(value.AsInt());
    case KX_VALUE_BOOL:
      return value.AsBool() ? "true" : "false";
    case KX_VALUE_FLOAT: {
      char buffer[64];
      std::snprintf(buffer, sizeof(buffer), "%.6g", static_cast<double>(value.AsFloat()));
      return std::string(buffer);
    }
    default:
      return std::string();
  }
}

bool ValuesEqual(const KX_RuntimeValue &a, const KX_RuntimeValue &b)
{
  const bool a_numeric = a.type == KX_VALUE_FLOAT || a.type == KX_VALUE_INT ||
                         a.type == KX_VALUE_BOOL;
  const bool b_numeric = b.type == KX_VALUE_FLOAT || b.type == KX_VALUE_INT ||
                         b.type == KX_VALUE_BOOL;
  if (a_numeric && b_numeric) {
    return a.AsFloat() == b.AsFloat();
  }
  if (a.type == KX_VALUE_STRING && b.type == KX_VALUE_STRING) {
    return std::string(a.AsString() != nullptr ? a.AsString() : "") ==
           std::string(b.AsString() != nullptr ? b.AsString() : "");
  }
  return a.AsPointer() == b.AsPointer() && a.type == b.type;
}

/* --------------------------------------------------------------- string nodes */

const KX_FuncPort kStringSplitInputs[] = {
    FP("Source", KX_VALUE_STRING),
    FP("Separator", KX_VALUE_STRING),
};
const KX_FuncPort kStringPartsOutput[] = {
    FP("Parts", KX_VALUE_LIST),
};

void FuncStringSplit(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const std::string source = InString(node, ctx, 0);
  const std::string separator = InString(node, ctx, 1);
  KX_ValueList &parts = ctx.Pool().AllocList();

  if (separator.empty()) {
    /* Empty separator splits on runs of ASCII whitespace. */
    size_t i = 0;
    while (i < source.size()) {
      while (i < source.size() && std::isspace(static_cast<unsigned char>(source[i]))) {
        ++i;
      }
      const size_t begin = i;
      while (i < source.size() && !std::isspace(static_cast<unsigned char>(source[i]))) {
        ++i;
      }
      if (i > begin && parts.size() < kMaxElements) {
        parts.push_back(KX_RuntimeValue::String(ctx.Pool().Intern(source.substr(begin, i - begin))));
      }
    }
  }
  else {
    /* Every character of Separator is a delimiter, so ",;" splits on both
     * commas and semicolons. Empty fields are preserved, including leading,
     * consecutive and trailing ones. */
    size_t start = 0;
    while (start <= source.size() && parts.size() < kMaxElements) {
      size_t end = start;
      while (end < source.size() && separator.find(source[end]) == std::string::npos) {
        ++end;
      }
      parts.push_back(KX_RuntimeValue::String(ctx.Pool().Intern(source.substr(start, end - start))));
      if (end >= source.size()) {
        break;
      }
      start = end + 1;
    }
  }
  node.Out(ctx, 0, KX_RuntimeValue::List(&parts));
}

const KX_FuncPort kStringJoinInputs[] = {
    FP("Parts", KX_VALUE_LIST),
    FP("Separator", KX_VALUE_STRING),
};
const KX_FuncPort kStringResultOutput[] = {
    FP("Result", KX_VALUE_STRING),
};

void FuncStringJoin(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const KX_ValueList *parts = InList(node, ctx, 0);
  const std::string separator = InString(node, ctx, 1);
  std::string result;
  if (parts != nullptr) {
    for (size_t i = 0; i < parts->size(); ++i) {
      if (i != 0) {
        result.append(separator);
      }
      result.append(ElementText((*parts)[i]));
    }
  }
  node.OutString(ctx, 0, result);
}

const KX_FuncPort kStringRegexMatchInputs[] = {
    FP("Source", KX_VALUE_STRING),
    FP("Pattern", KX_VALUE_STRING),
};
const KX_FuncPort kStringRegexMatchOutputs[] = {
    FP("Matched", KX_VALUE_BOOL),
    FP("Groups", KX_VALUE_LIST),
    FP("Rejected", KX_VALUE_BOOL),
};

void FuncStringRegexMatch(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  B6SWarnState *state = node.StateBlock<B6SWarnState>(ctx);
  const std::string source = InString(node, ctx, 0);
  const std::string pattern = InString(node, ctx, 1);
  KX_ValueList &groups = ctx.Pool().AllocList();
  bool matched = false;
  bool rejected = false;
  if (source.size() > kMaxRegexSource || pattern.size() > kMaxRegexPattern) {
    if (state != nullptr && !state->warned) {
      state->warned = true;
      std::fprintf(stderr,
                   "[StringRegexMatch] input too long (source=%zu pattern=%zu; limits %zu/%zu)\n",
                   source.size(),
                   pattern.size(),
                   kMaxRegexSource,
                   kMaxRegexPattern);
    }
    rejected = true;
  }
  else if (!IsSafeRegexPattern(pattern)) {
    if (state != nullptr && !state->warned) {
      state->warned = true;
      std::fprintf(stderr, "[Regex] unsafe pattern rejected: nested quantifier\n");
    }
    rejected = true;
  }
  else {
    try {
      const std::regex expression(pattern);
      std::smatch match;
      matched = std::regex_search(source, match, expression);
      if (matched) {
        for (size_t i = 0; i < match.size() && groups.size() < kMaxElements; ++i) {
          groups.push_back(KX_RuntimeValue::String(ctx.Pool().Intern(match[i].str())));
        }
      }
    }
    catch (const std::regex_error &error) {
      if (state != nullptr && !state->warned) {
        state->warned = true;
        std::fprintf(stderr,
                     "[StringRegexMatch] invalid pattern '%s': %s\n",
                     pattern.c_str(),
                     error.what());
      }
      rejected = true;
    }
  }
  node.Out(ctx, 0, KX_RuntimeValue::Bool(matched));
  node.Out(ctx, 1, KX_RuntimeValue::List(&groups));
  node.Out(ctx, 2, KX_RuntimeValue::Bool(rejected));
}

const KX_FuncPort kStringRegexReplaceInputs[] = {
    FP("Source", KX_VALUE_STRING),
    FP("Pattern", KX_VALUE_STRING),
    FP("Replacement", KX_VALUE_STRING),
};
const KX_FuncPort kStringRegexReplaceOutputs[] = {
    FP("Result", KX_VALUE_STRING),
    FP("Rejected", KX_VALUE_BOOL),
};

/* The runtime reads output ports by index and the editor mirrors them by name,
 * so the output counts are part of the port contract (Match: Matched=0,
 * Groups=1, Rejected=2; Replace: Result=0, Rejected=1). */
static_assert(B6S_ARRAY_LEN(kStringRegexMatchOutputs) == 3,
              "StringRegexMatch ABI: Matched/Groups/Rejected");
static_assert(B6S_ARRAY_LEN(kStringRegexReplaceOutputs) == 2,
              "StringRegexReplace ABI: Result/Rejected");
static_assert(sizeof(B6SWarnState) == 1, "B6SWarnState ABI: one bool");

void FuncStringRegexReplace(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  B6SWarnState *state = node.StateBlock<B6SWarnState>(ctx);
  const std::string source = InString(node, ctx, 0);
  const std::string pattern = InString(node, ctx, 1);
  const std::string replacement = InString(node, ctx, 2);
  std::string result = source;
  bool rejected = false;
  if (source.size() > kMaxRegexSource || pattern.size() > kMaxRegexPattern) {
    if (state != nullptr && !state->warned) {
      state->warned = true;
      std::fprintf(stderr,
                   "[StringRegexReplace] input too long (source=%zu pattern=%zu; limits %zu/%zu)\n",
                   source.size(),
                   pattern.size(),
                   kMaxRegexSource,
                   kMaxRegexPattern);
    }
    rejected = true;
  }
  else if (!IsSafeRegexPattern(pattern)) {
    if (state != nullptr && !state->warned) {
      state->warned = true;
      std::fprintf(stderr, "[Regex] unsafe pattern rejected: nested quantifier\n");
    }
    rejected = true;
  }
  else {
    try {
      const std::regex expression(pattern);
      result = std::regex_replace(source, expression, replacement);
    }
    catch (const std::regex_error &error) {
      if (state != nullptr && !state->warned) {
        state->warned = true;
        std::fprintf(stderr,
                     "[StringRegexReplace] invalid pattern '%s': %s\n",
                     pattern.c_str(),
                     error.what());
      }
      rejected = true;
    }
  }
  node.OutString(ctx, 0, result);
  node.Out(ctx, 1, KX_RuntimeValue::Bool(rejected));
}

const KX_FuncPort kStringTrimInputs[] = {
    FP("Source", KX_VALUE_STRING),
    FP("Mode", KX_VALUE_INT),
};

void FuncStringTrim(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const std::string source = InString(node, ctx, 0);
  const int mode = node.InInt(ctx, 1);
  size_t begin = 0;
  size_t end = source.size();
  if (mode != 2) {
    while (begin < end && std::isspace(static_cast<unsigned char>(source[begin]))) {
      ++begin;
    }
  }
  if (mode != 1) {
    while (end > begin && std::isspace(static_cast<unsigned char>(source[end - 1]))) {
      --end;
    }
  }
  node.OutString(ctx, 0, source.substr(begin, end - begin));
}

const KX_FuncPort kStringPadInputs[] = {
    FP("Source", KX_VALUE_STRING),
    FP("Length", KX_VALUE_INT),
    FP("PadChar", KX_VALUE_STRING),
    FP("Side", KX_VALUE_INT),
};

void FuncStringPad(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const std::string source = InString(node, ctx, 0);
  const int length = node.InInt(ctx, 1);
  const std::string pad_text = InString(node, ctx, 2);
  const int side = node.InInt(ctx, 3);
  if (length <= static_cast<int>(source.size()) || length > static_cast<int>(kMaxElements)) {
    node.OutString(ctx, 0, source);
    return;
  }
  const char pad = pad_text.empty() ? ' ' : pad_text[0];
  std::string result = source;
  const size_t missing = static_cast<size_t>(length) - source.size();
  if (side == 1) {
    result.append(missing, pad);
  }
  else {
    result.insert(result.begin(), missing, pad);
  }
  node.OutString(ctx, 0, result);
}

/* ------------------------------------------------------- UTF-8 char variants */

/**
 * Length in bytes of the UTF-8 sequence starting at #offset.
 *
 * Invalid lead bytes, truncated and malformed continuation sequences all
 * return 1 so the caller advances safely and counts them as one character.
 */
size_t Utf8SequenceLength(const std::string &text, size_t offset, bool *valid)
{
  const unsigned char lead = static_cast<unsigned char>(text[offset]);
  size_t length = 1;
  if (lead < 0x80) {
    length = 1;
  }
  else if ((lead & 0xE0) == 0xC0) {
    length = 2;
  }
  else if ((lead & 0xF0) == 0xE0) {
    length = 3;
  }
  else if ((lead & 0xF8) == 0xF0) {
    length = 4;
  }
  else {
    *valid = false;
    return 1;
  }
  if (offset + length > text.size()) {
    *valid = false;
    /* Truncated tail: consume it as a single tolerant character so a partial
     * multi byte sequence is not counted once per remaining byte. */
    return text.size() - offset;
  }
  for (size_t i = 1; i < length; ++i) {
    if ((static_cast<unsigned char>(text[offset + i]) & 0xC0) != 0x80) {
      *valid = false;
      return 1;
    }
  }
  *valid = true;
  return length;
}

size_t Utf8CharCount(const std::string &text, bool *invalid)
{
  size_t count = 0;
  size_t offset = 0;
  *invalid = false;
  while (offset < text.size()) {
    bool valid = true;
    const size_t length = Utf8SequenceLength(text, offset, &valid);
    if (!valid) {
      *invalid = true;
    }
    offset += length;
    ++count;
  }
  return count;
}

/** First UTF-8 sequence of #text, or a single space when empty/invalid. */
std::string Utf8PadUnit(const std::string &text, bool *invalid)
{
  if (text.empty()) {
    return std::string(" ");
  }
  bool valid = true;
  const size_t length = Utf8SequenceLength(text, 0, &valid);
  if (!valid) {
    *invalid = true;
    return std::string(" ");
  }
  return text.substr(0, length);
}

void WarnInvalidUtf8(bool invalid, B6SWarnState *state, const char *node_label)
{
  if (!invalid || state == nullptr || state->warned) {
    return;
  }
  std::fprintf(stderr,
               "[%s] invalid UTF-8 sequence: counted as one character\n",
               node_label);
  state->warned = true;
}

const KX_FuncPort kStringLengthCharsInputs[] = {
    FP("Source", KX_VALUE_STRING),
};
const KX_FuncPort kStringLengthCharsOutput[] = {
    FP("Count", KX_VALUE_INT),
};

void FuncStringLengthChars(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const std::string source = InString(node, ctx, 0);
  bool invalid = false;
  const size_t count = Utf8CharCount(source, &invalid);
  WarnInvalidUtf8(invalid, node.StateBlock<B6SWarnState>(ctx), "StringLengthChars");
  node.OutInt(ctx, 0, static_cast<int>(count));
}

const KX_FuncPort kStringPadCharsInputs[] = {
    FP("Source", KX_VALUE_STRING),
    FP("Length", KX_VALUE_INT),
    FP("PadChar", KX_VALUE_STRING),
    FP("Side", KX_VALUE_INT),
};

void FuncStringPadChars(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const std::string source = InString(node, ctx, 0);
  const int length = node.InInt(ctx, 1);
  const std::string pad_text = InString(node, ctx, 2);
  const int side = node.InInt(ctx, 3);
  bool invalid = false;
  const size_t count = Utf8CharCount(source, &invalid);
  const std::string unit = Utf8PadUnit(pad_text, &invalid);
  WarnInvalidUtf8(invalid, node.StateBlock<B6SWarnState>(ctx), "StringPadChars");
  if (length <= static_cast<int>(count) || length > static_cast<int>(kMaxElements)) {
    node.OutString(ctx, 0, source);
    return;
  }
  const size_t missing = static_cast<size_t>(length) - count;
  std::string fill;
  fill.reserve(missing * unit.size());
  for (size_t i = 0; i < missing; ++i) {
    fill.append(unit);
  }
  node.OutString(ctx, 0, side == 1 ? source + fill : fill + source);
}

/* ---------------------------------------------------------------- list nodes */

const KX_FuncPort kListSortInputs[] = {
    FP("List", KX_VALUE_LIST),
};
const KX_FuncPort kListSortOutput[] = {
    FP("SortedList", KX_VALUE_LIST),
};

struct SortKey {
  bool text = false;
  double number = 0.0;
  std::string text_value;
};

SortKey KeyOf(const KX_RuntimeValue &value,
              KX_GameObject *object,
              const std::string &property,
              KX_ExecutionContext &ctx)
{
  SortKey key;
  if (object != nullptr && !property.empty()) {
    KX_PropertyAccessor accessor(ctx.PropertyBackend());
    KX_PropertyCacheEntry cache;
    const KX_ValueType type = accessor.Resolve(object, property, &cache);
    KX_RuntimeValue current;
    if (type != KX_VALUE_NONE && accessor.Read(object, property, &cache, type, &current)) {
      if (type == KX_VALUE_STRING) {
        key.text = true;
        key.text_value = current.AsString() != nullptr ? current.AsString() : "";
        return key;
      }
      key.number = current.AsFloat();
      return key;
    }
  }
  if (value.type == KX_VALUE_STRING) {
    key.text = true;
    key.text_value = value.AsString() != nullptr ? value.AsString() : "";
  }
  else {
    key.number = value.AsFloat();
  }
  return key;
}

void FuncListSort(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const KX_ValueList *source = InList(node, ctx, 0);
  KX_ValueList &result = CopyIntoPool(ctx, source, 0);
  const std::string property = node.ConfigString("key");
  const bool descending = node.ConfigBool("descending", false);

  /* Key resolution needs the context; bucket each element once so the
   * comparator stays cheap and stable. */
  std::vector<KX_RuntimeValue> values(result.begin(), result.end());
  std::vector<SortKey> keys;
  keys.reserve(values.size());
  for (const KX_RuntimeValue &value : values) {
    KX_GameObject *object = value.type == KX_VALUE_OBJECT
                                ? static_cast<KX_GameObject *>(value.AsPointer())
                                : nullptr;
    keys.push_back(KeyOf(value, object, property, ctx));
  }

  std::vector<size_t> order(values.size());
  for (size_t i = 0; i < order.size(); ++i) {
    order[i] = i;
  }
  const bool descending_order = descending;
  auto less_key = [&keys](size_t a, size_t b) {
    if (keys[a].text && keys[b].text) {
      return keys[a].text_value < keys[b].text_value;
    }
    return keys[a].number < keys[b].number;
  };
  std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    /* Strict weak ordering for both directions: descending compares b < a. */
    return descending_order ? less_key(b, a) : less_key(a, b);
  });

  KX_ValueList &sorted = ctx.Pool().AllocList(order.size());
  for (size_t index : order) {
    sorted.push_back(values[index]);
  }
  node.Out(ctx, 0, KX_RuntimeValue::List(&sorted));
}

/* --------------------------------------------- small expression evaluator */

class NumericExpression {
 public:
  NumericExpression(const std::string &source, double x) : source_(source), x_(x) {}
  double Evaluate(std::string &error)
  {
    position_ = 0;
    error_.clear();
    double value = ParseOr();
    SkipSpaces();
    if (!error_.empty()) {
      error = error_;
      return 0.0;
    }
    if (position_ != source_.size()) {
      error = "unexpected token '" + source_.substr(position_) + "'";
      return 0.0;
    }
    return value;
  }

 private:
  void SkipSpaces()
  {
    while (position_ < source_.size() &&
           std::isspace(static_cast<unsigned char>(source_[position_])))
    {
      ++position_;
    }
  }
  bool Match(const char *token)
  {
    SkipSpaces();
    const size_t length = std::strlen(token);
    if (source_.compare(position_, length, token) == 0) {
      position_ += length;
      return true;
    }
    return false;
  }
  double ParseOr()
  {
    double value = ParseAnd();
    while (Match("||")) {
      const double right = ParseAnd();
      value = (value != 0.0 || right != 0.0) ? 1.0 : 0.0;
    }
    return value;
  }
  double ParseAnd()
  {
    double value = ParseCompare();
    while (Match("&&")) {
      const double right = ParseCompare();
      value = (value != 0.0 && right != 0.0) ? 1.0 : 0.0;
    }
    return value;
  }
  double ParseCompare()
  {
    double value = ParseAdd();
    for (;;) {
      if (Match("==")) {
        value = value == ParseAdd() ? 1.0 : 0.0;
      }
      else if (Match("!=")) {
        value = value != ParseAdd() ? 1.0 : 0.0;
      }
      else if (Match("<=")) {
        value = value <= ParseAdd() ? 1.0 : 0.0;
      }
      else if (Match(">=")) {
        value = value >= ParseAdd() ? 1.0 : 0.0;
      }
      else if (Match("<")) {
        value = value < ParseAdd() ? 1.0 : 0.0;
      }
      else if (Match(">")) {
        value = value > ParseAdd() ? 1.0 : 0.0;
      }
      else {
        break;
      }
    }
    return value;
  }
  double ParseAdd()
  {
    double value = ParseMul();
    for (;;) {
      if (Match("+")) {
        value += ParseMul();
      }
      else if (Match("-")) {
        value -= ParseMul();
      }
      else {
        break;
      }
    }
    return value;
  }
  double ParseMul()
  {
    double value = ParseUnary();
    for (;;) {
      if (Match("*")) {
        value *= ParseUnary();
      }
      else if (Match("/")) {
        const double right = ParseUnary();
        if (right == 0.0) {
          error_ = "division by zero";
          return 0.0;
        }
        value /= right;
      }
      else if (Match("%")) {
        const double right = ParseUnary();
        if (right == 0.0) {
          error_ = "modulo by zero";
          return 0.0;
        }
        value = std::fmod(value, right);
      }
      else {
        break;
      }
    }
    return value;
  }
  double ParseUnary()
  {
    if (Match("-")) {
      return -ParseUnary();
    }
    if (Match("!")) {
      return ParseUnary() == 0.0 ? 1.0 : 0.0;
    }
    return ParsePrimary();
  }
  double ParsePrimary()
  {
    SkipSpaces();
    if (position_ >= source_.size()) {
      error_ = "unexpected end of expression";
      return 0.0;
    }
    if (Match("(")) {
      const double value = ParseOr();
      if (!Match(")")) {
        error_ = "missing ')'";
      }
      return value;
    }
    const char c = source_[position_];
    if (c == 'x' || c == 'X') {
      ++position_;
      return x_;
    }
    if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
      size_t start = position_;
      while (position_ < source_.size() &&
             (std::isdigit(static_cast<unsigned char>(source_[position_])) ||
              source_[position_] == '.'))
      {
        ++position_;
      }
      const std::string text = source_.substr(start, position_ - start);
      char *end = nullptr;
      const double value = std::strtod(text.c_str(), &end);
      if (end == nullptr || *end != '\0') {
        error_ = "invalid number '" + text + "'";
        return 0.0;
      }
      return value;
    }
    error_ = std::string("unsupported token '") + c + "'";
    return 0.0;
  }

  const std::string &source_;
  double x_ = 0.0;
  size_t position_ = 0;
  std::string error_;
};

const KX_FuncPort kListFilterInputs[] = {
    FP("List", KX_VALUE_LIST),
    FP("Predicate", KX_VALUE_STRING),
};
const KX_FuncPort kListFilterOutput[] = {
    FP("FilteredList", KX_VALUE_LIST),
};

void FuncListFilter(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  B6SWarnState *state = node.StateBlock<B6SWarnState>(ctx);
  const KX_ValueList *source = InList(node, ctx, 0);
  const std::string predicate = InString(node, ctx, 1);
  KX_ValueList &result = ctx.Pool().AllocList();
  if (source == nullptr) {
    node.Out(ctx, 0, KX_RuntimeValue::List(&result));
    return;
  }
  for (const KX_RuntimeValue &value : *source) {
    NumericExpression expression(predicate, value.AsFloat());
    std::string error;
    const double keep = expression.Evaluate(error);
    if (!error.empty()) {
      if (state != nullptr && !state->warned) {
        state->warned = true;
        std::fprintf(stderr,
                     "[ListFilter] unsupported predicate '%s': %s\n",
                     predicate.c_str(),
                     error.c_str());
      }
      break;
    }
    if (keep != 0.0 && result.size() < kMaxElements) {
      result.push_back(value);
    }
  }
  node.Out(ctx, 0, KX_RuntimeValue::List(&result));
}

const KX_FuncPort kListMapInputs[] = {
    FP("List", KX_VALUE_LIST),
    FP("Expression", KX_VALUE_STRING),
};
const KX_FuncPort kListMapOutput[] = {
    FP("MappedList", KX_VALUE_LIST),
};

void FuncListMap(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  B6SWarnState *state = node.StateBlock<B6SWarnState>(ctx);
  const KX_ValueList *source = InList(node, ctx, 0);
  const std::string expression_text = InString(node, ctx, 1);
  KX_ValueList &result = ctx.Pool().AllocList();
  if (source == nullptr) {
    node.Out(ctx, 0, KX_RuntimeValue::List(&result));
    return;
  }
  for (const KX_RuntimeValue &value : *source) {
    NumericExpression expression(expression_text, value.AsFloat());
    std::string error;
    const double mapped = expression.Evaluate(error);
    if (!error.empty()) {
      if (state != nullptr && !state->warned) {
        state->warned = true;
        std::fprintf(stderr,
                     "[ListMap] unsupported expression '%s': %s\n",
                     expression_text.c_str(),
                     error.c_str());
      }
      result.clear();
      break;
    }
    if (result.size() < kMaxElements) {
      result.push_back(KX_RuntimeValue::Float(static_cast<float>(mapped)));
    }
  }
  node.Out(ctx, 0, KX_RuntimeValue::List(&result));
}

const KX_FuncPort kListFindInputs[] = {
    FP("List", KX_VALUE_LIST),
    FP("Value", KX_VALUE_FLOAT),
};
const KX_FuncPort kListFindOutputs[] = {
    FP("Index", KX_VALUE_INT),
    FP("Found", KX_VALUE_BOOL),
};

void FuncListFind(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const KX_ValueList *source = InList(node, ctx, 0);
  const KX_RuntimeValue value = node.In(ctx, 1);
  int index = -1;
  if (source != nullptr) {
    for (size_t i = 0; i < source->size(); ++i) {
      if (ValuesEqual((*source)[i], value)) {
        index = static_cast<int>(i);
        break;
      }
    }
  }
  node.Out(ctx, 0, KX_RuntimeValue::Int(index));
  node.Out(ctx, 1, KX_RuntimeValue::Bool(index >= 0));
}

const KX_FuncPort kListSliceInputs[] = {
    FP("List", KX_VALUE_LIST),
    FP("Start", KX_VALUE_INT),
    FP("End", KX_VALUE_INT),
};
const KX_FuncPort kListSliceOutput[] = {
    FP("Slice", KX_VALUE_LIST),
};

void FuncListSlice(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const KX_ValueList *source = InList(node, ctx, 0);
  const size_t size = source != nullptr ? source->size() : 0;
  const int start_value = node.InInt(ctx, 1);
  const int end_value = node.InInt(ctx, 2);
  /* B6.9-S1 negative index contract (documented in B6_STRING_LIST.md):
   *   value >= 0       -> that absolute position, clamped to [0, size]
   *   value <  0       -> counts from the end (-1 = last element,
   *                       -size = first element); values below -size clamp
   *                       to 0.
   *   End == 0 keeps the legacy sentinel "through the end" so existing graphs
   *   keep working; use -1 when the last element must be excluded. */
  auto resolve_index = [size](int value) -> size_t {
    if (value >= 0) {
      return std::min(static_cast<size_t>(value), size);
    }
    const int from_end = static_cast<int>(size) + value;
    return from_end <= 0 ? 0 : std::min(static_cast<size_t>(from_end), size);
  };
  const size_t start = resolve_index(start_value);
  size_t end = end_value == 0 ? size : resolve_index(end_value);
  if (end < start) {
    end = start;
  }
  KX_ValueList &result = ctx.Pool().AllocList(end - start);
  for (size_t i = start; i < end; ++i) {
    result.push_back((*source)[i]);
  }
  node.Out(ctx, 0, KX_RuntimeValue::List(&result));
}

const KX_FuncPort kListConcatInputs[] = {
    FP("ListA", KX_VALUE_LIST),
    FP("ListB", KX_VALUE_LIST),
};
const KX_FuncPort kListConcatOutput[] = {
    FP("Result", KX_VALUE_LIST),
};

void FuncListConcat(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const KX_ValueList *a = InList(node, ctx, 0);
  const KX_ValueList *b = InList(node, ctx, 1);
  const size_t size_a = a != nullptr ? a->size() : 0;
  const size_t size_b = b != nullptr ? b->size() : 0;
  KX_ValueList &result = ctx.Pool().AllocList(std::min(size_a + size_b, kMaxElements));
  if (a != nullptr) {
    for (const KX_RuntimeValue &value : *a) {
      if (result.size() >= kMaxElements) {
        break;
      }
      result.push_back(value);
    }
  }
  if (b != nullptr) {
    for (const KX_RuntimeValue &value : *b) {
      if (result.size() >= kMaxElements) {
        break;
      }
      result.push_back(value);
    }
  }
  node.Out(ctx, 0, KX_RuntimeValue::List(&result));
}

const KX_FuncPort kListReverseInputs[] = {
    FP("List", KX_VALUE_LIST),
};
const KX_FuncPort kListReverseOutput[] = {
    FP("ReversedList", KX_VALUE_LIST),
};

void FuncListReverse(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const KX_ValueList *source = InList(node, ctx, 0);
  KX_ValueList &result = ctx.Pool().AllocList();
  if (source != nullptr) {
    for (size_t i = source->size(); i > 0 && result.size() < kMaxElements; --i) {
      result.push_back((*source)[i - 1]);
    }
  }
  node.Out(ctx, 0, KX_RuntimeValue::List(&result));
}

const KX_FuncPort kListUniqueInputs[] = {
    FP("List", KX_VALUE_LIST),
};
const KX_FuncPort kListUniqueOutput[] = {
    FP("UniqueList", KX_VALUE_LIST),
};

void FuncListUnique(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const KX_ValueList *source = InList(node, ctx, 0);
  KX_ValueList &result = ctx.Pool().AllocList();
  if (source != nullptr) {
    for (const KX_RuntimeValue &value : *source) {
      if (result.size() >= kMaxElements) {
        break;
      }
      bool seen = false;
      for (const KX_RuntimeValue &existing : result) {
        if (ValuesEqual(existing, value)) {
          seen = true;
          break;
        }
      }
      if (!seen) {
        result.push_back(value);
      }
    }
  }
  node.Out(ctx, 0, KX_RuntimeValue::List(&result));
}

const KX_FuncPort kListIndexOfInputs[] = {
    FP("List", KX_VALUE_LIST),
    FP("Value", KX_VALUE_FLOAT),
};
const KX_FuncPort kListIndexOfOutput[] = {
    FP("Index", KX_VALUE_INT),
};

void FuncListIndexOf(KX_FunctionNode &node, KX_ExecutionContext &ctx)
{
  const KX_ValueList *source = InList(node, ctx, 0);
  const KX_RuntimeValue value = node.In(ctx, 1);
  int index = -1;
  if (source != nullptr) {
    for (size_t i = 0; i < source->size(); ++i) {
      if (ValuesEqual((*source)[i], value)) {
        index = static_cast<int>(i);
        break;
      }
    }
  }
  node.OutInt(ctx, 0, index);
}

/* ------------------------------------------------------------------- Registry */

const KX_FunctionSpec kB6StringSpecs[] = {
    B6S_VALUE("StringSplit", kStringSplitInputs, kStringPartsOutput, FuncStringSplit),
    B6S_VALUE("StringJoin", kStringJoinInputs, kStringResultOutput, FuncStringJoin),
    B6S_VALUE_STATE("StringRegexMatch",
                    kStringRegexMatchInputs,
                    kStringRegexMatchOutputs,
                    FuncStringRegexMatch,
                    B6SWarnState),
    B6S_VALUE_STATE("StringRegexReplace",
                    kStringRegexReplaceInputs,
                    kStringRegexReplaceOutputs,
                    FuncStringRegexReplace,
                    B6SWarnState),
    B6S_VALUE("StringTrim", kStringTrimInputs, kStringResultOutput, FuncStringTrim),
    B6S_VALUE("StringPad", kStringPadInputs, kStringResultOutput, FuncStringPad),
    B6S_VALUE_STATE("StringLengthChars",
                    kStringLengthCharsInputs,
                    kStringLengthCharsOutput,
                    FuncStringLengthChars,
                    B6SWarnState),
    B6S_VALUE_STATE("StringPadChars",
                    kStringPadCharsInputs,
                    kStringResultOutput,
                    FuncStringPadChars,
                    B6SWarnState),
    B6S_VALUE("ListSort", kListSortInputs, kListSortOutput, FuncListSort),
    B6S_VALUE_STATE("ListFilter",
                    kListFilterInputs,
                    kListFilterOutput,
                    FuncListFilter,
                    B6SWarnState),
    B6S_VALUE_STATE("ListMap", kListMapInputs, kListMapOutput, FuncListMap, B6SWarnState),
    B6S_VALUE("ListFind", kListFindInputs, kListFindOutputs, FuncListFind),
    B6S_VALUE("ListSlice", kListSliceInputs, kListSliceOutput, FuncListSlice),
    B6S_VALUE("ListConcat", kListConcatInputs, kListConcatOutput, FuncListConcat),
    B6S_VALUE("ListReverse", kListReverseInputs, kListReverseOutput, FuncListReverse),
    B6S_VALUE("ListUnique", kListUniqueInputs, kListUniqueOutput, FuncListUnique),
    B6S_VALUE("ListIndexOf", kListIndexOfInputs, kListIndexOfOutput, FuncListIndexOf),
};

KX_NodeSignature MakeB6StringSignature(const KX_FunctionSpec &spec)
{
  KX_NodeSignature signature;
  for (uint32_t i = 0; i < spec.input_count; ++i) {
    KX_NodePortSignature port;
    port.name = spec.inputs[i].name;
    port.kind = spec.inputs[i].trigger ? IRPortKind::Trigger : IRPortKind::Value;
    port.value_type = spec.inputs[i].trigger ? KX_VALUE_NONE : spec.inputs[i].type;
    signature.inputs.push_back(port);
  }
  for (uint32_t i = 0; i < spec.output_count; ++i) {
    KX_NodePortSignature port;
    port.name = spec.outputs[i].name;
    port.kind = spec.outputs[i].trigger ? IRPortKind::Trigger : IRPortKind::Value;
    port.value_type = spec.outputs[i].trigger ? KX_VALUE_NONE : spec.outputs[i].type;
    signature.outputs.push_back(port);
  }
  signature.state_size = spec.state_size;
  signature.state_alignment = spec.state_alignment;
  signature.needs_tick = spec.needs_tick || spec.tick != nullptr;
  signature.breaks_trigger_cycles = spec.breaks_trigger_cycles;
  signature.ports_from_ir = spec.ports_from_ir;
  return signature;
}

}  // namespace

void RegisterB6StringFunctionGameNodes()
{
  for (const KX_FunctionSpec &spec : kB6StringSpecs) {
    const KX_FunctionSpec *entry = &spec;
    KX_NodeRegistry::Get().RegisterType(
        spec.type_id,
        std::string("KX_FuncB6String_") + spec.type_id,
        MakeB6StringSignature(*entry),
        [entry]() { return std::unique_ptr<KX_GameNodeRuntime>(new KX_FunctionNode(entry)); });
  }
}
