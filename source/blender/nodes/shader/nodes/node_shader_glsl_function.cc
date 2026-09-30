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
