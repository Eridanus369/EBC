/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup shdnodes
 *
 * Stage-1 skeleton of the NPR GLSL Function node.
 * See docs/plans/glsl-function-port-plan.md for the full port plan.
 */

#include "node_shader_util.hh"
#include "node_util.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "BLI_math_vector_types.hh"
#include "BLI_utildefines.hh"
#include "BLI_set.hh"
#include "BLI_span.hh"

#include "RNA_types.hh"

namespace blender {

namespace nodes::node_shader_glsl_function_cc {

/* ============ boundary_type ============ */
    enum class GLSLBoundaryType
    {
      Unsupported = 0,
      Float,
      Int,
      Bool,
      Vec2,
      Vec3,
      Vec4,
      Mat2,
      Mat3,
      Mat4,
      Sample2D,
      Sample3D,
      Void,
    };

/* ============ token ============ */
    struct GLSLToken
    {
      enum class Kind
      {
        Identifier,
        Punctuation,
      };

      Kind kind;
      std::string text;
      char punctuation = '\0';
      int64_t source_start = 0;
      int64_t source_end = 0;
    };

/* ============ function_param ============ */
    struct GLSLFunctionParam
    {
      enum class Qualifier
      {
        In,
        Out,
        InOut,
      };

      struct IntChoiceItem
      {
        int value = 0;
        std::string label;
      };

      struct Meta
      {
        bool has_default_value = false;
        float4 default_value = float4(0.0f);
        std::optional<int> int_default_value;
        std::optional<std::string> default_expression;
        std::optional<std::string> panel_name;
        std::optional<std::string> description;
        std::optional<std::string> label;
        bool has_min = false;
        float min_value = 0.0f;
        std::optional<int> int_min_value;
        bool has_max = false;
        float max_value = 0.0f;
        std::optional<int> int_max_value;
        bool hide_value = false;
        std::optional<PropertySubType> subtype;
        Vector<IntChoiceItem> int_choices;
        bool int_choices_show_label = false;

        bool has_any() const
        {
          return has_default_value || default_expression.has_value() || panel_name.has_value() ||
            description.has_value() || label.has_value() || has_min || has_max || hide_value ||
            subtype.has_value() || !int_choices.is_empty() || int_choices_show_label;
        }
      };

      GLSLBoundaryType type = GLSLBoundaryType::Unsupported;
      Qualifier qualifier = Qualifier::In;
      std::string type_name;
      std::string name;
      std::string identifier;
      int dimensions = 0;
      bool is_array = false;
      int64_t type_source_start = -1;
      int64_t type_source_end = -1;
      Meta meta;
    };

/* ============ panel_meta ============ */
    struct GLSLPanelMeta
    {
      std::string name;
      bool default_closed = true;
    };


/* ============ define_meta ============ */
    struct GLSLDefineMeta
    {
      std::string name;
      int type = SHD_GLSL_FUNCTION_DEFINE_BOOL;
      int default_value = 0;
      std::optional<int> min_value;
      std::optional<int> max_value;
      std::optional<std::string> label;
      std::optional<std::string> description;
      Vector<GLSLFunctionParam::IntChoiceItem> int_choices;
      bool int_choices_show_label = false;
    };


/* ============ define_value_state ============ */
    struct GLSLDefineValueState
    {
      char name[64] = "";
      int type = SHD_GLSL_FUNCTION_DEFINE_BOOL;
      int value = 0;
    };


/* ============ source_range ============ */
    struct GLSLSourceRange
    {
      int64_t start = 0;
      int64_t end = 0;
    };


/* ============ function_definition ============ */
    struct GLSLFunctionDefinition
    {
      std::string name;
      std::string return_type_name;
      GLSLBoundaryType return_type = GLSLBoundaryType::Unsupported;
      GLSLFunctionParam::Meta return_meta;
      Vector<GLSLFunctionParam> params;
      Vector<GLSLPanelMeta> panels;
      int body_token_start = -1;
      int body_token_end = -1;
      int64_t body_source_start = -1;
      int64_t body_source_end = -1;
    };


/* ============ identifier_helpers ============ */
    [[maybe_unused]] static bool is_identifier_start(const char c)
    {
      return std::isalpha(uchar(c)) || c == '_';
    }

    [[maybe_unused]] static bool is_identifier_continue(const char c)
    {
      return std::isalnum(uchar(c)) || c == '_';
    }

/* ============ stage 2b: top-level extraction ============ */
    [[maybe_unused]] static Vector<std::string> find_top_level_glsl_function_names(const Vector<GLSLToken>& tokens)
    {
      Vector<std::string> names;
      int brace_depth = 0;

      brace_depth = 0;
      for (int i = 0; i < tokens.size(); i++)
      {
        const GLSLToken& token = tokens[i];
        if (token.kind == GLSLToken::Kind::Punctuation)
        {
          if (token.punctuation == '{')
          {
            brace_depth++;
          }
          else if (token.punctuation == '}')
          {
            brace_depth = max_ii(0, brace_depth - 1);
          }
          else if (token.punctuation == '(' && brace_depth == 0 && i >= 2 &&
            tokens[i - 1].kind == GLSLToken::Kind::Identifier &&
            tokens[i - 2].kind == GLSLToken::Kind::Identifier)
          {
            int paren_depth = 1;
            int closing_paren_index = -1;
            for (int j = i + 1; j < tokens.size(); j++)
            {
              if (tokens[j].kind == GLSLToken::Kind::Punctuation)
              {
                if (tokens[j].punctuation == '(')
                {
                  paren_depth++;
                }
                else if (tokens[j].punctuation == ')')
                {
                  paren_depth--;
                  if (paren_depth == 0)
                  {
                    closing_paren_index = j;
                    break;
                  }
                }
              }
            }
            if (closing_paren_index == -1)
            {
              continue;
            }
            const int next_index = closing_paren_index + 1;
            if (next_index < tokens.size() && tokens[next_index].kind == GLSLToken::Kind::Punctuation &&
              tokens[next_index].punctuation == '{')
            {
              names.append(tokens[i - 1].text);
            }
          }
        }
      }
      return names;
    }

    [[maybe_unused]] static bool validate_top_level_glsl_declarations(const Vector<GLSLToken>& tokens,
      std::string& r_error)
    {
      int brace_depth = 0;
      Vector<std::string> statement_identifiers;

      auto reset_statement = [&]() { statement_identifiers.clear(); };
      auto validate_statement = [&]() -> bool
        {
          if (statement_identifiers.is_empty())
          {
            return true;
          }

          const StringRef first_identifier = statement_identifiers.first();
          if (first_identifier == "precision")
          {
            r_error = "Top-level precision declarations are not supported";
            return false;
          }
          if (first_identifier == "uniform")
          {
            r_error = "Top-level uniform declarations are not supported; expose them as function parameters";
            return false;
          }
          if (first_identifier == "layout")
          {
            r_error = "Top-level layout-qualified declarations are not supported";
            return false;
          }
          if (ELEM(first_identifier, "in", "out", "attribute", "varying", "buffer"))
          {
            r_error = "Top-level shader interface declarations are not supported";
            return false;
          }
          if (ELEM(first_identifier, "flat", "smooth", "noperspective", "centroid", "sample") &&
            (statement_identifiers.contains("in") || statement_identifiers.contains("out")))
          {
            r_error = "Top-level shader interface declarations are not supported";
            return false;
          }
          return true;
        };

      auto flush_statement = [&]() -> bool
        {
          if (!validate_statement())
          {
            return false;
          }
          reset_statement();
          return true;
        };

      for (int i = 0; i < tokens.size(); i++)
      {
        const GLSLToken& token = tokens[i];
        if (token.kind == GLSLToken::Kind::Punctuation)
        {
          if (token.punctuation == '{')
          {
            brace_depth++;
            reset_statement();
          }
          else if (token.punctuation == '}')
          {
            brace_depth = max_ii(0, brace_depth - 1);
          }
          else if (token.punctuation == ';' && brace_depth == 0)
          {
            if (!flush_statement())
            {
              return false;
            }
          }
          continue;
        }

        if (brace_depth == 0 && token.kind == GLSLToken::Kind::Identifier)
        {
          if (i >= 2 && tokens[i - 1].kind == GLSLToken::Kind::Identifier &&
            tokens[i].kind == GLSLToken::Kind::Identifier && (i + 1) < tokens.size() &&
            tokens[i + 1].kind == GLSLToken::Kind::Punctuation && tokens[i + 1].punctuation == '(' &&
            !statement_identifiers.is_empty())
          {
            if (!flush_statement())
            {
              return false;
            }
          }
          statement_identifiers.append(token.text);
        }
      }

      if (brace_depth == 0 && !validate_statement())
      {
        return false;
      }

      return true;
    }

    [[maybe_unused]] static Vector<std::string> find_top_level_glsl_global_names(
      const Vector<GLSLToken>& tokens, const Span<std::string> function_names)
    {
      Set<std::string> function_name_set;
      for (const std::string& function_name : function_names)
      {
        function_name_set.add(function_name);
      }

      Set<std::string> global_name_set;
      Vector<std::string> global_names;
      int brace_depth = 0;
      int paren_depth = 0;
      int bracket_depth = 0;
      std::string last_identifier;
      bool last_identifier_is_function_name = false;

      auto flush_identifier = [&]()
        {
          if (last_identifier.empty())
          {
            return;
          }
          if (!last_identifier_is_function_name && !function_name_set.contains(last_identifier) &&
            global_name_set.add(last_identifier))
          {
            global_names.append(last_identifier);
          }
          last_identifier.clear();
          last_identifier_is_function_name = false;
        };

      for (int i = 0; i < tokens.size(); i++)
      {
        const GLSLToken& token = tokens[i];
        if (token.kind == GLSLToken::Kind::Punctuation)
        {
          switch (token.punctuation)
          {
          case '{':
            brace_depth++;
            flush_identifier();
            break;
          case '}':
            brace_depth = max_ii(0, brace_depth - 1);
            flush_identifier();
            break;
          case '(':
            if (brace_depth == 0 && paren_depth == 0 && bracket_depth == 0 && !last_identifier.empty())
            {
              last_identifier_is_function_name = true;
            }
            paren_depth++;
            break;
          case ')':
            paren_depth = max_ii(0, paren_depth - 1);
            break;
          case '[':
            bracket_depth++;
            break;
          case ']':
            bracket_depth = max_ii(0, bracket_depth - 1);
            break;
          case ',':
          case '=':
          case ';':
            if (brace_depth == 0 && paren_depth == 0 && bracket_depth == 0)
            {
              flush_identifier();
            }
            break;
          default:
            break;
          }
          continue;
        }

        if (brace_depth == 0 && paren_depth == 0 && bracket_depth == 0 &&
          token.kind == GLSLToken::Kind::Identifier)
        {
          last_identifier = token.text;
          last_identifier_is_function_name = false;
        }
      }

      return global_names;
    }

/* ============ tokenize_impl ============ */
    [[maybe_unused]] static Vector<GLSLToken> tokenize_glsl_source(const StringRef source,
                                                const bool include_preprocessor)
    {
      Vector<GLSLToken> tokens;
      bool beginning_of_line = true;

      for (int64_t i = 0; i < source.size();)
      {
        const char c = source[i];

        if (c == '\n')
        {
          beginning_of_line = true;
          i++;
          continue;
        }
        if (std::isspace(uchar(c)))
        {
          i++;
          continue;
        }
        if (beginning_of_line && c == '#' && !include_preprocessor)
        {
          while (i < source.size() && source[i] != '\n')
          {
            i++;
          }
          continue;
        }

        beginning_of_line = false;

        if (ELEM(c, '\"', '\''))
        {
          const char quote = c;
          i++;
          while (i < source.size())
          {
            if (source[i] == '\\' && (i + 1) < source.size())
            {
              i += 2;
              continue;
            }
            if (source[i++] == quote)
            {
              break;
            }
          }
          continue;
        }

        if (is_identifier_start(c))
        {
          const int64_t start = i;
          i++;
          while (i < source.size() && is_identifier_continue(source[i]))
          {
            i++;
          }
          tokens.append(
            { GLSLToken::Kind::Identifier, std::string(source.substr(start, i - start)), '\0', start, i });
          continue;
        }

        if (strchr("(){}[],;=", c) != nullptr)
        {
          tokens.append({ GLSLToken::Kind::Punctuation, std::string(1, c), c, i, i + 1 });
        }
        i++;
      }

      return tokens;
    }


static void node_declare(NodeDeclarationBuilder &b)
{
  /* Stage-1 skeleton: static socket layout. Real sockets will be derived
   * from the parsed GLSL function signature in a later porting stage. */
  b.is_function_node();
  b.add_input<decl::Float>("Value"_ustr).default_value(0.0f);
  b.add_output<decl::Float>("Value"_ustr);
}

static void node_init(bNodeTree * /*ntree*/, bNode *node)
{
  node->storage = MEM_new<NodeShaderGLSLFunction>("NodeShaderGLSLFunction");
}

static void node_layout(ui::Layout &layout, bContext * /*C*/, PointerRNA * /*ptr*/)
{
  layout.label("GLSL Function (stage-1 skeleton, not yet functional)", ICON_INFO);
}

static int node_shader_gpu_glsl_function(GPUMaterial * /*mat*/,
                                         bNode * /*node*/,
                                         bNodeExecData * /*execdata*/,
                                         GPUNodeStack * /*in*/,
                                         GPUNodeStack * /*out*/)
{
  /* Stage-1 skeleton: no GPU implementation yet. Returning 0 makes the
   * link fail cleanly instead of feeding wrong data downstream. */
  return 0;
}

}  // namespace nodes::node_shader_glsl_function_cc

void register_node_type_sh_glsl_function()
{
  namespace file_ns = nodes::node_shader_glsl_function_cc;

  static bke::bNodeType ntype;

  sh_node_type_base(&ntype, "ShaderNodeGLSLFunction"_ustr, SH_NODE_GLSL_FUNCTION);
  ntype.ui_name = "GLSL Function";
  ntype.ui_description =
      "Evaluate a user-authored GLSL function on the GPU.\n"
      "(stage-1 skeleton, not yet functional)";
  ntype.enum_name_legacy = "GLSL_FUNCTION";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.draw_buttons = file_ns::node_layout;
  ntype.initfunc = file_ns::node_init;
  ntype.gpu_fn = file_ns::node_shader_gpu_glsl_function;
  bke::node_type_storage(
      ntype, "NodeShaderGLSLFunction", node_free_standard_storage, node_copy_standard_storage);

  bke::node_register_type(ntype);
}

}  // namespace blender
