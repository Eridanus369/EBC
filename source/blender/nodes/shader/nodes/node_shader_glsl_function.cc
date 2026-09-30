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

#include "BKE_text.h"

#include "BLI_math_vector_types.hh"
#include "BLI_utildefines.hh"
#include <sstream>

#include "BLI_map.hh"
#include "BLI_set.hh"
#include "BLI_span.hh"
#include "BLI_string_ref.hh"

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

/* ============ stage 2b (part 2): function definition parsing ============ */
        [[maybe_unused]] static GLSLBoundaryType glsl_boundary_type_from_name(const StringRef type_name)
    {
      if (type_name == "float")
      {
        return GLSLBoundaryType::Float;
      }
      if (type_name == "int")
      {
        return GLSLBoundaryType::Int;
      }
      if (type_name == "bool")
      {
        return GLSLBoundaryType::Bool;
      }
      if (type_name == "vec2")
      {
        return GLSLBoundaryType::Vec2;
      }
      if (type_name == "vec3")
      {
        return GLSLBoundaryType::Vec3;
      }
      if (type_name == "vec4")
      {
        return GLSLBoundaryType::Vec4;
      }
      if (type_name == "mat2")
      {
        return GLSLBoundaryType::Mat2;
      }
      if (type_name == "mat3")
      {
        return GLSLBoundaryType::Mat3;
      }
      if (type_name == "mat4")
      {
        return GLSLBoundaryType::Mat4;
      }
      if (type_name == "sampler2D")
      {
        return GLSLBoundaryType::Sample2D;
      }
      if (type_name == "sampler3D")
      {
        return GLSLBoundaryType::Sample3D;
      }
      if (type_name == "void")
      {
        return GLSLBoundaryType::Void;
      }
      return GLSLBoundaryType::Unsupported;
    }

    [[maybe_unused]] static int glsl_boundary_dimensions(const GLSLBoundaryType type)
    {
      switch (type)
      {
      case GLSLBoundaryType::Vec2:
        return 2;
      case GLSLBoundaryType::Vec3:
        return 3;
      case GLSLBoundaryType::Vec4:
        return 4;
      default:
        return 0;
      }
    }

    [[maybe_unused]] static bool glsl_boundary_type_is_sampler(const GLSLBoundaryType type)
    {
      return ELEM(type, GLSLBoundaryType::Sample2D, GLSLBoundaryType::Sample3D);
    }

    [[maybe_unused]] static std::string make_socket_identifier(const StringRef prefix, const StringRef name)
    {
      std::string identifier;
      identifier.reserve(prefix.size() + name.size() + 8);
      identifier.append(prefix);
      identifier.push_back('_');

      if (name.is_empty())
      {
        identifier.append("value");
      }
      else
      {
        for (const char c : name)
        {
          identifier.push_back(is_identifier_continue(c) ? c : '_');
        }
      }

      if (!identifier.empty() && std::isdigit(uchar(identifier.back())))
      {
        identifier.push_back('_');
      }
      return identifier;
    }

    [[maybe_unused]] static bool glsl_param_has_input_socket(const GLSLFunctionParam& param)
    {
      return ELEM(param.qualifier,
        GLSLFunctionParam::Qualifier::In,
        GLSLFunctionParam::Qualifier::InOut);
    }

    [[maybe_unused]] static bool glsl_param_has_output_socket(const GLSLFunctionParam& param)
    {
      return ELEM(param.qualifier,
        GLSLFunctionParam::Qualifier::Out,
        GLSLFunctionParam::Qualifier::InOut);
    }

    [[maybe_unused]] static int glsl_function_output_count(const GLSLFunctionDefinition& function)
    {
      int count = function.return_type == GLSLBoundaryType::Void ? 0 : 1;
      for (const GLSLFunctionParam& param : function.params)
      {
        if (glsl_param_has_output_socket(param))
        {
          count++;
        }
      }
      return count;
    }

    [[maybe_unused]] static bool parse_glsl_parameter_tokens(const Span<GLSLToken> tokens,
      GLSLFunctionParam& r_param,
      std::string& r_error)
    {
      if (tokens.is_empty())
      {
        r_error = "Empty parameter declaration";
        return false;
      }

      Vector<const GLSLToken*> identifiers;
      bool has_out_qualifier = false;
      bool has_inout_qualifier = false;
      bool has_array_declarator = false;
      bool has_unsupported_punctuation = false;
      int array_bracket_depth = 0;

      for (const GLSLToken& token : tokens)
      {
        if (token.kind == GLSLToken::Kind::Punctuation && token.punctuation == '[')
        {
          has_array_declarator = true;
          array_bracket_depth++;
        }
        else if (token.kind == GLSLToken::Kind::Punctuation && token.punctuation == ']')
        {
          has_array_declarator = true;
          array_bracket_depth = std::max(array_bracket_depth - 1, 0);
        }
        else if (token.kind == GLSLToken::Kind::Identifier && array_bracket_depth == 0)
        {
          identifiers.append(&token);
          has_out_qualifier |= token.text == "out";
          has_inout_qualifier |= token.text == "inout";
        }
        else if (token.kind == GLSLToken::Kind::Punctuation)
        {
          has_unsupported_punctuation = true;
        }
      }

      if (has_out_qualifier && has_inout_qualifier)
      {
        r_error = "A parameter cannot be both 'out' and 'inout'";
        return false;
      }
      if (has_unsupported_punctuation)
      {
        r_error = "Unsupported GLSL parameter syntax";
        return false;
      }
      if (identifiers.size() == 1 && identifiers[0]->text == "void")
      {
        r_param = {};
        return true;
      }
      if (identifiers.size() < 2)
      {
        r_error = "Each parameter needs a type and a name";
        return false;
      }

      const GLSLToken& type_token = *identifiers[identifiers.size() - 2];
      const StringRef type_name = type_token.text;
      const StringRef param_name = identifiers.last()->text;
      const GLSLBoundaryType boundary_type = glsl_boundary_type_from_name(type_name);
      if (!ELEM(boundary_type,
        GLSLBoundaryType::Float,
        GLSLBoundaryType::Int,
        GLSLBoundaryType::Bool,
        GLSLBoundaryType::Vec2,
        GLSLBoundaryType::Vec3,
        GLSLBoundaryType::Vec4,
        GLSLBoundaryType::Mat2,
        GLSLBoundaryType::Mat3,
        GLSLBoundaryType::Mat4,
        GLSLBoundaryType::Sample2D,
        GLSLBoundaryType::Sample3D))
      {
        r_error =
          "Supported parameter types are float, int, bool, vec2, vec3, vec4, mat2, mat3, mat4, "
          "sampler2D, and sampler3D";
        return false;
      }

      r_param.type = boundary_type;
      if (has_inout_qualifier)
      {
        r_error = "The 'inout' qualifier is not supported yet";
        return false;
      }
      if (has_out_qualifier && glsl_boundary_type_is_sampler(boundary_type))
      {
        r_error = "sampler parameters only support input qualifiers";
        return false;
      }
      r_param.qualifier = has_out_qualifier ? GLSLFunctionParam::Qualifier::Out :
        GLSLFunctionParam::Qualifier::In;
      r_param.type_name = type_name;
      r_param.name = std::string(param_name);
      r_param.identifier = make_socket_identifier(has_out_qualifier ? "Out" : "In", param_name);
      r_param.dimensions = glsl_boundary_dimensions(boundary_type);
      r_param.is_array = has_array_declarator;
      r_param.type_source_start = type_token.source_start;
      r_param.type_source_end = type_token.source_end;
      return true;
    }

    [[maybe_unused]] static bool parse_glsl_function_definition(const Vector<GLSLToken>& tokens,
      const int paren_index,
      const int closing_paren_index,
      GLSLFunctionDefinition& r_function,
      std::string& r_error)
    {
      if (paren_index < 2 || tokens[paren_index].punctuation != '(' ||
        tokens[closing_paren_index].punctuation != ')')
      {
        r_error = "Malformed GLSL function declaration";
        return false;
      }

      const GLSLToken& name_token = tokens[paren_index - 1];
      const GLSLToken& type_token = tokens[paren_index - 2];
      if (name_token.kind != GLSLToken::Kind::Identifier ||
        type_token.kind != GLSLToken::Kind::Identifier)
      {
        r_error = "Could not resolve function name and return type";
        return false;
      }

      r_function.name = name_token.text;
      r_function.return_type_name = type_token.text;
      r_function.return_type = glsl_boundary_type_from_name(type_token.text);
      if (!ELEM(r_function.return_type,
        GLSLBoundaryType::Void,
        GLSLBoundaryType::Float,
        GLSLBoundaryType::Int,
        GLSLBoundaryType::Bool,
        GLSLBoundaryType::Vec2,
        GLSLBoundaryType::Vec3,
        GLSLBoundaryType::Vec4,
        GLSLBoundaryType::Mat2,
        GLSLBoundaryType::Mat3,
        GLSLBoundaryType::Mat4))
      {
        r_error =
          "Supported return types are void, float, int, bool, vec2, vec3, vec4, mat2, mat3, and "
          "mat4";
        return false;
      }

      Vector<GLSLToken> parameter_tokens;
      int parameter_depth = 0;
      for (int i = paren_index + 1; i < closing_paren_index; i++)
      {
        const GLSLToken& token = tokens[i];
        if (token.kind == GLSLToken::Kind::Punctuation && token.punctuation == ',' &&
          parameter_depth == 0)
        {
          GLSLFunctionParam parameter;
          if (!parse_glsl_parameter_tokens(parameter_tokens, parameter, r_error))
          {
            return false;
          }
          if (parameter.type != GLSLBoundaryType::Unsupported)
          {
            r_function.params.append(parameter);
          }
          parameter_tokens.clear();
          continue;
        }
        if (token.kind == GLSLToken::Kind::Punctuation)
        {
          if (token.punctuation == '(')
          {
            parameter_depth++;
          }
          else if (token.punctuation == ')')
          {
            parameter_depth--;
          }
        }
        parameter_tokens.append(token);
      }

      if (!parameter_tokens.is_empty())
      {
        GLSLFunctionParam parameter;
        if (!parse_glsl_parameter_tokens(parameter_tokens, parameter, r_error))
        {
          return false;
        }
        if (parameter.type != GLSLBoundaryType::Unsupported)
        {
          r_function.params.append(parameter);
        }
      }

      if (r_function.return_type == GLSLBoundaryType::Void && glsl_function_output_count(r_function) == 0)
      {
        r_error = "The selected function does not expose any node outputs";
        return false;
      }

      const int opening_brace_index = closing_paren_index + 1;
      if (opening_brace_index >= tokens.size() || tokens[opening_brace_index].kind != GLSLToken::Kind::Punctuation ||
        tokens[opening_brace_index].punctuation != '{')
      {
        r_error = "Could not resolve the GLSL function body";
        return false;
      }

      int brace_depth = 1;
      int closing_brace_index = -1;
      for (int i = opening_brace_index + 1; i < tokens.size(); i++)
      {
        const GLSLToken& token = tokens[i];
        if (token.kind != GLSLToken::Kind::Punctuation)
        {
          continue;
        }
        if (token.punctuation == '{')
        {
          brace_depth++;
        }
        else if (token.punctuation == '}')
        {
          brace_depth--;
          if (brace_depth == 0)
          {
            closing_brace_index = i;
            break;
          }
        }
      }
      if (closing_brace_index == -1)
      {
        r_error = "Could not resolve the GLSL function body";
        return false;
      }
      r_function.body_token_start = opening_brace_index + 1;
      r_function.body_token_end = closing_brace_index - 1;
      r_function.body_source_start = tokens[opening_brace_index].source_end;
      r_function.body_source_end = tokens[closing_brace_index].source_start;

      return true;
    }

    [[maybe_unused]] static bool find_glsl_function_definition(const Vector<GLSLToken>& tokens,
      const StringRef function_name,
      GLSLFunctionDefinition& r_function,
      std::string& r_error)
    {
      int brace_depth = 0;
      bool found_first_function = false;

      for (int i = 0; i < tokens.size(); i++)
      {
        const GLSLToken& token = tokens[i];

        if (token.kind == GLSLToken::Kind::Punctuation && token.punctuation == '{')
        {
          brace_depth++;
          continue;
        }
        if (token.kind == GLSLToken::Kind::Punctuation && token.punctuation == '}')
        {
          brace_depth = max_ii(0, brace_depth - 1);
          continue;
        }

        if (brace_depth != 0 || token.kind != GLSLToken::Kind::Punctuation || token.punctuation != '(' ||
          i < 2 || tokens[i - 1].kind != GLSLToken::Kind::Identifier ||
          tokens[i - 2].kind != GLSLToken::Kind::Identifier)
        {
          continue;
        }

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

        if (closing_paren_index == -1 || (closing_paren_index + 1) >= tokens.size() ||
          tokens[closing_paren_index + 1].kind != GLSLToken::Kind::Punctuation ||
          tokens[closing_paren_index + 1].punctuation != '{')
        {
          continue;
        }

        const StringRef candidate_name = tokens[i - 1].text;
        if (!function_name.is_empty() && candidate_name != function_name)
        {
          found_first_function = true;
          continue;
        }

        if (!parse_glsl_function_definition(tokens, i, closing_paren_index, r_function, r_error))
        {
          return false;
        }
        return true;
      }

      if (function_name.is_empty())
      {
        r_error = found_first_function ? "Could not parse the first GLSL function definition" :
          "No GLSL function definition was found";
      }
      else
      {
        r_error = "The selected function was not found in the source";
      }
      return false;
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


static constexpr const char *K_GLSLFN_DEFAULT_SOURCE =
    "vec3 glslfn(vec3 color)\n"
    "{\n"
    "  return color * 0.5;\n"
    "}\n";

static std::string glslfn_read_source(const bNode *node)
{
  if (node == nullptr || node->id == nullptr) {
    return K_GLSLFN_DEFAULT_SOURCE;
  }
  Text *text = reinterpret_cast<Text *>(node->id);
  if (text == nullptr) {
    return K_GLSLFN_DEFAULT_SOURCE;
  }
  size_t len = 0;
  char *buf = txt_to_buf(text, &len);
  std::string source;
  if (buf != nullptr && len > 0) {
    source.assign(buf, len);
  }
  MEM_delete(buf);
  if (source.empty()) {
    source = K_GLSLFN_DEFAULT_SOURCE;
  }
  return source;
}

/* Pick which top-level function to bind: the node's explicit function_name if it
 * matches one of the found names, otherwise the first declared function. */
static std::string glslfn_pick_function(const bNode *node,
                                        const Vector<std::string> &names)
{
  if (names.is_empty()) {
    return {};
  }
  if (node != nullptr && node->storage != nullptr) {
    const NodeShaderGLSLFunction *storage =
        static_cast<const NodeShaderGLSLFunction *>(node->storage);
    if (storage->function_name[0] != '\0') {
      const std::string want = storage->function_name;
      for (const std::string &n : names) {
        if (n == want) {
          return want;
        }
      }
    }
  }
  return names[0];
}


/* ============ Stage 3 T4: @glsl_meta parsing (lite: no panels/items) ============ */

    struct GLSLRawParamMeta
    {
      std::optional<std::string> default_value;
      std::optional<std::string> min_value;
      std::optional<std::string> max_value;
      std::optional<std::string> hide_value;
      std::optional<std::string> subtype;
      std::optional<std::string> description;
      std::optional<std::string> label;

      bool has_any() const
      {
        return default_value.has_value() || min_value.has_value() || max_value.has_value() ||
          hide_value.has_value() || subtype.has_value() || description.has_value() ||
          label.has_value();
      }
    };

    [[maybe_unused]] static std::string trim_copy(const StringRef text)
    {
      int64_t start = 0;
      int64_t end = text.size();
      while (start < end && std::isspace(uchar(text[start]))) {
        start++;
      }
      while (end > start && std::isspace(uchar(text[end - 1]))) {
        end--;
      }
      return text.substr(start, end - start);
    }

    [[maybe_unused]] static std::string lowercase_copy(StringRef text)
    {
      std::string result(text);
      for (char &c : result) {
        c = std::tolower(uchar(c));
      }
      return result;
    }

    [[maybe_unused]] static std::string strip_glsl_comments(StringRef source)
    {
      std::string stripped;
      stripped.reserve(source.size());
      for (int64_t i = 0; i < source.size();) {
        if (ELEM(source[i], '"', '\'')) {
          const char quote = source[i];
          stripped.push_back(source[i++]);
          while (i < source.size()) {
            const char c = source[i];
            stripped.push_back(c);
            i++;
            if (c == '\\' && i < source.size()) {
              stripped.push_back(source[i++]);
              continue;
            }
            if (c == quote) {
              break;
            }
          }
          continue;
        }
        if ((i + 1) < source.size() && source[i] == '/' && source[i + 1] == '/') {
          stripped.append("  ");
          i += 2;
          while (i < source.size() && source[i] != '\n') {
            stripped.push_back(source[i] == '\r' ? '\r' : ' ');
            i++;
          }
          continue;
        }
        if ((i + 1) < source.size() && source[i] == '/' && source[i + 1] == '*') {
          stripped.append("  ");
          i += 2;
          while ((i + 1) < source.size() && !(source[i] == '*' && source[i + 1] == '/')) {
            stripped.push_back(ELEM(source[i], '\r', '\n') ? source[i] : ' ');
            i++;
          }
          if ((i + 1) < source.size()) {
            stripped.append("  ");
            i += 2;
          }
          else if (i < source.size()) {
            stripped.push_back(source[i++]);
          }
          continue;
        }
        stripped.push_back(source[i]);
        i++;
      }
      return stripped;
    }

    [[maybe_unused]] static std::string make_glsl_meta_key(const StringRef function_name,
                                                           const StringRef param_name)
    {
      std::string key;
      key.reserve(function_name.size() + param_name.size() + 1);
      key.append(function_name);
      key.push_back('\x1f');
      key.append(param_name);
      return key;
    }

    [[maybe_unused]] static void split_glsl_meta_key(const StringRef key,
                                                     StringRef &r_function_name,
                                                     StringRef &r_param_name)
    {
      const int64_t separator = key.find('\x1f');
      if (separator == StringRef::not_found) {
        r_function_name = "";
        r_param_name = key;
        return;
      }
      r_function_name = key.substr(0, separator);
      r_param_name = key.substr(separator + 1);
    }

    [[maybe_unused]] static bool parse_glsl_meta_bool_literal(const StringRef text,
                                                              bool &r_value,
                                                              std::string &r_error)
    {
      const std::string normalized = lowercase_copy(trim_copy(text));
      if (normalized == "1" || normalized == "true" || normalized == "yes" ||
          normalized == "on")
      {
        r_value = true;
        return true;
      }
      if (normalized == "0" || normalized == "false" || normalized == "no" ||
          normalized == "off")
      {
        r_value = false;
        return true;
      }
      r_error = "Expected a GLSL meta boolean literal";
      return false;
    }

    [[maybe_unused]] static bool parse_glsl_meta_int_literal(const StringRef text,
                                                             int &r_value,
                                                             std::string &r_error)
    {
      const std::string trimmed = trim_copy(text);
      if (trimmed.empty()) {
        r_error = "GLSL meta integer value cannot be empty";
        return false;
      }
      char *end = nullptr;
      errno = 0;
      const long long value = std::strtoll(trimmed.c_str(), &end, 10);
      if (end == trimmed.c_str() || *end != '\0') {
        r_error = "Could not parse GLSL meta integer value '" + trimmed + "'";
        return false;
      }
      if (errno == ERANGE || value < INT_MIN || value > INT_MAX) {
        r_error = "GLSL meta integer value '" + trimmed + "' is outside the int32 range";
        return false;
      }
      r_value = int(value);
      return true;
    }

    [[maybe_unused]] static bool parse_glsl_meta_float_literal(const StringRef text,
                                                               float &r_value,
                                                               std::string &r_error)
    {
      const std::string trimmed = trim_copy(text);
      if (trimmed.empty()) {
        r_error = "GLSL meta float value cannot be empty";
        return false;
      }
      char *end = nullptr;
      errno = 0;
      const float value = std::strtof(trimmed.c_str(), &end);
      if (end == trimmed.c_str() || trim_copy(end).size() != 0) {
        r_error = "Could not parse GLSL meta float value '" + trimmed + "'";
        return false;
      }
      if (errno == ERANGE || !std::isfinite(value)) {
        r_error = "GLSL meta float value '" + trimmed + "' is outside the finite float range";
        return false;
      }
      r_value = value;
      return true;
    }

    [[maybe_unused]] static bool parse_glsl_meta_vector_default(const StringRef text,
                                                                const int dimensions,
                                                                float4 &r_value,
                                                                std::string &r_error)
    {
      const std::string trimmed = trim_copy(text);
      const std::string prefix = "vec" + std::to_string(dimensions);
      if (!StringRef(trimmed).startswith(prefix) || !StringRef(trimmed).endswith(")")) {
        r_error = "GLSL meta vector defaults must use " + prefix + "(...)";
        return false;
      }
      const StringRef args_text = StringRef(trimmed).substr(prefix.size());
      if (args_text.size() < 2 || args_text[0] != '(' ||
          args_text[args_text.size() - 1] != ')')
      {
        r_error = "Malformed GLSL meta vector constructor";
        return false;
      }
      Vector<std::string> args;
      int paren_depth = 0;
      int64_t arg_start = 1;
      for (int64_t i = 1; i < args_text.size() - 1; i++) {
        const char c = args_text[i];
        if (c == '(') {
          paren_depth++;
        }
        else if (c == ')') {
          paren_depth = std::max(paren_depth - 1, 0);
        }
        else if (c == ',' && paren_depth == 0) {
          args.append(trim_copy(args_text.substr(arg_start, i - arg_start)));
          arg_start = i + 1;
        }
      }
      args.append(trim_copy(args_text.substr(arg_start, args_text.size() - 1 - arg_start)));
      if (!(args.size() == 1 || args.size() == dimensions)) {
        r_error = "GLSL meta vector defaults must provide either one scalar or " +
                  std::to_string(dimensions) + " scalars";
        return false;
      }
      r_value = float4(0.0f);
      if (args.size() == 1) {
        float scalar = 0.0f;
        if (!parse_glsl_meta_float_literal(args[0], scalar, r_error)) {
          return false;
        }
        for (const int i : IndexRange(dimensions)) {
          r_value[i] = scalar;
        }
        return true;
      }
      for (const int i : IndexRange(dimensions)) {
        if (!parse_glsl_meta_float_literal(args[i], r_value[i], r_error)) {
          return false;
        }
      }
      return true;
    }

    [[maybe_unused]] static bool parse_glsl_meta_subtype(const StringRef text,
                                                         const GLSLBoundaryType type,
                                                         PropertySubType &r_subtype,
                                                         std::string &r_error)
    {
      std::string name = lowercase_copy(trim_copy(text));
      if (StringRef(name).startswith("prop_")) {
        name = name.substr(5);
      }
      if (type == GLSLBoundaryType::Float) {
        if (name == "none") {
          r_subtype = PROP_NONE;
        }
        else if (name == "unsigned") {
          r_subtype = PROP_UNSIGNED;
        }
        else if (name == "percentage") {
          r_subtype = PROP_PERCENTAGE;
        }
        else if (name == "factor") {
          r_subtype = PROP_FACTOR;
        }
        else if (name == "mass") {
          r_subtype = PROP_MASS;
        }
        else if (name == "angle") {
          r_subtype = PROP_ANGLE;
        }
        else if (name == "time") {
          r_subtype = PROP_TIME;
        }
        else if (name == "time_absolute") {
          r_subtype = PROP_TIME_ABSOLUTE;
        }
        else if (name == "distance") {
          r_subtype = PROP_DISTANCE;
        }
        else if (name == "wavelength") {
          r_subtype = PROP_WAVELENGTH;
        }
        else {
          r_error = "Unsupported GLSL meta float subtype '" + std::string(text) + "'";
          return false;
        }
        return true;
      }
      if (!ELEM(type, GLSLBoundaryType::Vec2, GLSLBoundaryType::Vec3, GLSLBoundaryType::Vec4))
      {
        r_error = "GLSL meta subtype is only supported for float and vec* inputs";
        return false;
      }
      if (name == "none") {
        r_subtype = PROP_NONE;
      }
      else if (name == "factor") {
        r_subtype = PROP_FACTOR;
      }
      else if (name == "percentage") {
        r_subtype = PROP_PERCENTAGE;
      }
      else if (name == "translation") {
        r_subtype = PROP_TRANSLATION;
      }
      else if (name == "direction") {
        r_subtype = PROP_DIRECTION;
      }
      else if (name == "velocity") {
        r_subtype = PROP_VELOCITY;
      }
      else if (name == "acceleration") {
        r_subtype = PROP_ACCELERATION;
      }
      else if (name == "euler") {
        r_subtype = PROP_EULER;
      }
      else if (name == "xyz") {
        r_subtype = PROP_XYZ;
      }
      else if (name == "color") {
        if (!ELEM(type, GLSLBoundaryType::Vec3, GLSLBoundaryType::Vec4)) {
          r_error = "GLSL meta vector subtype 'color' requires vec3 or vec4";
          return false;
        }
        r_subtype = PROP_COLOR;
      }
      else {
        r_error = "Unsupported GLSL meta vector subtype '" + std::string(text) + "'";
        return false;
      }
      return true;
    }

    [[maybe_unused]] static bool parse_glsl_meta_quoted_string(const StringRef text,
                                                               int64_t &r_index,
                                                               std::string &r_value,
                                                               std::string &r_error)
    {
      BLI_assert(r_index < text.size() && text[r_index] == '"');
      r_index++;
      while (r_index < text.size()) {
        const char c = text[r_index];
        r_index++;
        if (c == '"') {
          return true;
        }
        if (c == '\\') {
          if (r_index >= text.size()) {
            r_error = "Unterminated escape sequence in GLSL meta quoted string";
            return false;
          }
          const char escaped = text[r_index];
          r_index++;
          if (ELEM(escaped, '"', '\\')) {
            r_value.push_back(escaped);
            continue;
          }
          r_error = "Unsupported escape sequence in GLSL meta quoted string";
          return false;
        }
        r_value.push_back(c);
      }
      r_error = "Unterminated GLSL meta quoted string";
      return false;
    }

    [[maybe_unused]] static bool parse_glsl_meta_assignment_list(
        const StringRef text, Map<std::string, std::string> &r_assignments, std::string &r_error)
    {
      for (int64_t i = 0; i < text.size();) {
        while (i < text.size() && std::isspace(uchar(text[i]))) {
          i++;
        }
        if (i >= text.size()) {
          break;
        }
        if (!is_identifier_start(text[i])) {
          r_error = "Malformed GLSL meta attribute list";
          return false;
        }
        const int64_t key_start = i;
        i++;
        while (i < text.size() && is_identifier_continue(text[i])) {
          i++;
        }
        const std::string key = std::string(text.substr(key_start, i - key_start));
        while (i < text.size() && std::isspace(uchar(text[i]))) {
          i++;
        }
        if (i >= text.size() || text[i] != '=') {
          r_error = "GLSL meta attributes must use key=value syntax";
          return false;
        }
        i++;
        while (i < text.size() && std::isspace(uchar(text[i]))) {
          i++;
        }
        if (i >= text.size()) {
          r_error = "GLSL meta attribute is missing a value";
          return false;
        }
        std::string value;
        if (text[i] == '"') {
          if (!parse_glsl_meta_quoted_string(text, i, value, r_error)) {
            return false;
          }
          if (i < text.size() && !std::isspace(uchar(text[i]))) {
            r_error = "GLSL meta quoted attribute values must be followed by whitespace";
            return false;
          }
        }
        else {
          const int64_t value_start = i;
          int paren_depth = 0;
          while (i < text.size()) {
            const char c = text[i];
            if (c == '(') {
              paren_depth++;
            }
            else if (c == ')') {
              paren_depth = std::max(paren_depth - 1, 0);
            }
            else if (paren_depth == 0 && std::isspace(uchar(c))) {
              break;
            }
            i++;
          }
          value = std::string(text.substr(value_start, i - value_start));
        }
        value = trim_copy(value);
        if (value.empty()) {
          r_error = "GLSL meta attribute is missing a value";
          return false;
        }
        if (r_assignments.contains(key)) {
          r_error = "Duplicate GLSL meta attribute '" + key + "'";
          return false;
        }
        r_assignments.add(key, value);
      }
      return true;
    }

    [[maybe_unused]] static bool merge_glsl_raw_param_meta(GLSLRawParamMeta &r_meta,
                                                           const Map<std::string, std::string> &assignments,
                                                           std::string &r_error)
    {
      auto assign_once = [&](std::optional<std::string> &slot,
                             const StringRef key,
                             const StringRef value) -> bool {
        if (slot.has_value()) {
          r_error = "Duplicate GLSL meta attribute '" + std::string(key) + "'";
          return false;
        }
        slot = std::string(value);
        return true;
      };
      for (const auto &item : assignments.items()) {
        const StringRef key = item.key;
        const StringRef value = item.value;
        if (key == "default") {
          if (!assign_once(r_meta.default_value, key, value)) {
            return false;
          }
        }
        else if (key == "min") {
          if (!assign_once(r_meta.min_value, key, value)) {
            return false;
          }
        }
        else if (key == "max") {
          if (!assign_once(r_meta.max_value, key, value)) {
            return false;
          }
        }
        else if (key == "hide_value") {
          if (!assign_once(r_meta.hide_value, key, value)) {
            return false;
          }
        }
        else if (key == "subtype") {
          if (!assign_once(r_meta.subtype, key, value)) {
            return false;
          }
        }
        else if (key == "description") {
          if (!assign_once(r_meta.description, key, value)) {
            return false;
          }
        }
        else if (key == "label") {
          if (!assign_once(r_meta.label, key, value)) {
            return false;
          }
        }
        else {
          r_error = "Unsupported GLSL meta attribute '" + std::string(key) + "'";
          return false;
        }
      }
      return true;
    }

    [[maybe_unused]] static bool parse_glsl_meta_block(
        const StringRef comment,
        Map<std::string, GLSLRawParamMeta> &r_param_meta_by_name,
        bool &r_is_meta_block,
        std::string &r_error)
    {
      r_is_meta_block = false;
      std::stringstream stream{std::string(comment)};
      std::string line;
      bool header_seen = false;
      while (std::getline(stream, line)) {
        std::string normalized = trim_copy(line);
        if (!normalized.empty() && normalized[0] == '*') {
          normalized = trim_copy(StringRef(normalized).drop_prefix(1));
        }
        if (normalized.empty()) {
          continue;
        }
        if (!header_seen) {
          if (!StringRef(normalized).startswith("@glsl_meta")) {
            return true;
          }
          header_seen = true;
          r_is_meta_block = true;
          continue;
        }
        if (StringRef(normalized).startswith("@")) {
          r_error = "Unsupported GLSL meta directive '" + normalized + "'";
          return false;
        }
        const int64_t separator = StringRef(normalized).find(':');
        if (separator == StringRef::not_found) {
          r_error = "GLSL meta lines must use 'name: key=value' syntax";
          return false;
        }
        std::string target = trim_copy(StringRef(normalized).substr(0, separator));
        const std::string attributes_text = trim_copy(StringRef(normalized).substr(separator + 1));
        if (target.empty() || attributes_text.empty()) {
          r_error = "GLSL meta lines must define a target and at least one attribute";
          return false;
        }
        const std::string param_name = target;
        Map<std::string, std::string> assignments;
        if (!parse_glsl_meta_assignment_list(attributes_text, assignments, r_error)) {
          return false;
        }
        GLSLRawParamMeta meta;
        if (const GLSLRawParamMeta *existing = r_param_meta_by_name.lookup_ptr(param_name)) {
          meta = *existing;
        }
        if (!merge_glsl_raw_param_meta(meta, assignments, r_error)) {
          return false;
        }
        r_param_meta_by_name.add_overwrite(param_name, meta);
      }
      return true;
    }

    [[maybe_unused]] static bool find_glsl_meta_target_function_name(
        const StringRef source_after_comment, std::string &r_function_name, std::string &r_error)
    {
      const std::string stripped_source = strip_glsl_comments(source_after_comment);
      const Vector<GLSLToken> tokens = tokenize_glsl_source(stripped_source, false);
      if (tokens.is_empty()) {
        r_error = "GLSL meta block must be placed directly above a function definition";
        return false;
      }
      int brace_depth = 0;
      for (int i = 0; i < tokens.size(); i++) {
        const GLSLToken &token = tokens[i];
        if (token.kind != GLSLToken::Kind::Punctuation) {
          continue;
        }
        if (token.punctuation == '{') {
          if (brace_depth == 0) {
            r_error = "GLSL meta block must be placed directly above a function definition";
            return false;
          }
          brace_depth++;
          continue;
        }
        if (token.punctuation == '}') {
          brace_depth = max_ii(0, brace_depth - 1);
          continue;
        }
        if (brace_depth != 0) {
          continue;
        }
        if (token.punctuation == ';') {
          r_error = "GLSL meta block must be placed directly above a function definition";
          return false;
        }
        if (token.punctuation != '(' || i < 2 ||
            tokens[i - 1].kind != GLSLToken::Kind::Identifier ||
            tokens[i - 2].kind != GLSLToken::Kind::Identifier)
        {
          continue;
        }
        int paren_depth = 1;
        int closing_paren_index = -1;
        for (int j = i + 1; j < tokens.size(); j++) {
          if (tokens[j].kind != GLSLToken::Kind::Punctuation) {
            continue;
          }
          if (tokens[j].punctuation == '(') {
            paren_depth++;
          }
          else if (tokens[j].punctuation == ')') {
            paren_depth--;
            if (paren_depth == 0) {
              closing_paren_index = j;
              break;
            }
          }
        }
        if (closing_paren_index != -1 && (closing_paren_index + 1) < tokens.size() &&
            tokens[closing_paren_index + 1].kind == GLSLToken::Kind::Punctuation &&
            tokens[closing_paren_index + 1].punctuation == '{')
        {
          r_function_name = tokens[i - 1].text;
          return true;
        }
        r_error = "GLSL meta block must be placed directly above a function definition";
        return false;
      }
      r_error = "GLSL meta block must be placed directly above a function definition";
      return false;
    }

    [[maybe_unused]] static bool extract_glsl_meta(
        const StringRef source,
        Map<std::string, GLSLRawParamMeta> &r_meta_by_key,
        std::string &r_error)
    {
      Set<std::string> functions_with_meta;
      for (int64_t i = 0; (i + 1) < source.size();) {
        if (source[i] == '/' && source[i + 1] == '*') {
          const int64_t body_start = i + 2;
          int64_t body_end = source.size();
          bool found_end = false;
          for (int64_t j = body_start; (j + 1) < source.size(); j++) {
            if (source[j] == '*' && source[j + 1] == '/') {
              body_end = j;
              i = j + 2;
              found_end = true;
              break;
            }
          }
          if (!found_end) {
            r_error = "Unterminated GLSL block comment";
            return false;
          }
          Map<std::string, GLSLRawParamMeta> param_meta_by_name;
          bool is_meta_block = false;
          if (!parse_glsl_meta_block(
                  source.substr(body_start, body_end - body_start),
                  param_meta_by_name,
                  is_meta_block,
                  r_error))
          {
            return false;
          }
          if (!is_meta_block) {
            continue;
          }
          std::string function_name;
          if (!find_glsl_meta_target_function_name(source.substr(i), function_name, r_error)) {
            return false;
          }
          if (functions_with_meta.contains(function_name)) {
            r_error = "Only one GLSL meta block is supported per function";
            return false;
          }
          functions_with_meta.add(function_name);
          for (const auto &item : param_meta_by_name.items()) {
            r_meta_by_key.add(make_glsl_meta_key(function_name, item.key), item.value);
          }
          continue;
        }
        i++;
      }
      return true;
    }

    [[maybe_unused]] static bool apply_glsl_meta_to_param(const GLSLRawParamMeta &raw_meta,
                                                          GLSLFunctionParam &r_param,
                                                          std::string &r_error)
    {
      if (!raw_meta.has_any()) {
        return true;
      }
      if (!glsl_param_has_input_socket(r_param)) {
        if (glsl_param_has_output_socket(r_param) && raw_meta.label.has_value() &&
            !raw_meta.default_value.has_value() && !raw_meta.min_value.has_value() &&
            !raw_meta.max_value.has_value() && !raw_meta.hide_value.has_value() &&
            !raw_meta.subtype.has_value() && !raw_meta.description.has_value())
        {
          r_param.meta.label = *raw_meta.label;
          return true;
        }
        r_error = "GLSL meta only supports input parameters (label allowed on outputs)";
        return false;
      }
      if (glsl_boundary_type_is_sampler(r_param.type)) {
        if (raw_meta.default_value.has_value() || raw_meta.min_value.has_value() ||
            raw_meta.max_value.has_value() || raw_meta.hide_value.has_value() ||
            raw_meta.subtype.has_value())
        {
          r_error = "GLSL meta default/min/max/hide_value/subtype not supported for samplers";
          return false;
        }
        if (raw_meta.description.has_value()) {
          r_param.meta.description = *raw_meta.description;
        }
        if (raw_meta.label.has_value()) {
          r_param.meta.label = *raw_meta.label;
        }
        return true;
      }

      if (raw_meta.default_value.has_value()) {
        if (r_param.type == GLSLBoundaryType::Float) {
          float v = 0.0f;
          if (parse_glsl_meta_float_literal(*raw_meta.default_value, v, r_error)) {
            r_param.meta.default_value.x = v;
            r_param.meta.has_default_value = true;
          }
          else {
            r_param.meta.default_expression = trim_copy(*raw_meta.default_value);
            r_param.meta.hide_value = true;
            r_error.clear();
          }
        }
        else if (r_param.type == GLSLBoundaryType::Int) {
          int v = 0;
          if (parse_glsl_meta_int_literal(*raw_meta.default_value, v, r_error)) {
            r_param.meta.int_default_value = v;
            r_param.meta.has_default_value = true;
          }
          else {
            r_error.clear();
            r_error = "GLSL meta int default must be an integer literal";
            return false;
          }
        }
        else if (r_param.type == GLSLBoundaryType::Bool) {
          bool v = false;
          if (parse_glsl_meta_bool_literal(*raw_meta.default_value, v, r_error)) {
            r_param.meta.default_value.x = v ? 1.0f : 0.0f;
            r_param.meta.has_default_value = true;
          }
          else {
            r_error.clear();
            r_error = "GLSL meta bool default must be a boolean literal";
            return false;
          }
        }
        else {
          float4 v = float4(0.0f);
          if (parse_glsl_meta_vector_default(*raw_meta.default_value,
                                             r_param.dimensions, v, r_error))
          {
            r_param.meta.default_value = v;
            r_param.meta.has_default_value = true;
          }
          else {
            r_error.clear();
            r_error = "GLSL meta vector default must use vec" +
                      std::to_string(r_param.dimensions) + "(...)";
            return false;
          }
        }
      }

      if (raw_meta.min_value.has_value()) {
        if (r_param.type == GLSLBoundaryType::Int) {
          int v = 0;
          if (!parse_glsl_meta_int_literal(*raw_meta.min_value, v, r_error)) {
            return false;
          }
          r_param.meta.int_min_value = v;
        }
        else if (r_param.type == GLSLBoundaryType::Bool) {
          r_error = "GLSL meta min not supported for bool";
          return false;
        }
        else if (!parse_glsl_meta_float_literal(*raw_meta.min_value,
                                                r_param.meta.min_value, r_error))
        {
          return false;
        }
        r_param.meta.has_min = true;
      }

      if (raw_meta.max_value.has_value()) {
        if (r_param.type == GLSLBoundaryType::Int) {
          int v = 0;
          if (!parse_glsl_meta_int_literal(*raw_meta.max_value, v, r_error)) {
            return false;
          }
          r_param.meta.int_max_value = v;
        }
        else if (r_param.type == GLSLBoundaryType::Bool) {
          r_error = "GLSL meta max not supported for bool";
          return false;
        }
        else if (!parse_glsl_meta_float_literal(*raw_meta.max_value,
                                                r_param.meta.max_value, r_error))
        {
          return false;
        }
        r_param.meta.has_max = true;
      }

      if (raw_meta.hide_value.has_value()) {
        if (!parse_glsl_meta_bool_literal(*raw_meta.hide_value,
                                          r_param.meta.hide_value, r_error))
        {
          return false;
        }
      }

      if (r_param.meta.has_min && r_param.meta.has_max) {
        const bool invalid =
            r_param.type == GLSLBoundaryType::Int ?
                r_param.meta.int_min_value.value_or(0) > r_param.meta.int_max_value.value_or(0) :
                r_param.meta.min_value > r_param.meta.max_value;
        if (invalid) {
          r_error = "GLSL meta min cannot be greater than max";
          return false;
        }
      }

      if (raw_meta.subtype.has_value()) {
        PropertySubType subtype = PROP_NONE;
        if (!parse_glsl_meta_subtype(*raw_meta.subtype, r_param.type, subtype, r_error)) {
          return false;
        }
        r_param.meta.subtype = subtype;
      }

      if (raw_meta.description.has_value()) {
        r_param.meta.description = *raw_meta.description;
      }
      if (raw_meta.label.has_value()) {
        r_param.meta.label = *raw_meta.label;
      }
      return true;
    }

    [[maybe_unused]] static bool apply_glsl_meta_to_function(
        const Map<std::string, GLSLRawParamMeta> &meta_by_key,
        GLSLFunctionDefinition &r_function,
        std::string &r_error)
    {
      Set<std::string> param_names;
      for (const GLSLFunctionParam &param : r_function.params) {
        param_names.add(param.name);
      }
      for (const auto &item : meta_by_key.items()) {
        StringRef target_function;
        StringRef target_param;
        split_glsl_meta_key(item.key, target_function, target_param);
        if (!target_function.is_empty() && target_function != r_function.name) {
          continue;
        }
        if (!param_names.contains(std::string(target_param))) {
          r_error = "GLSL meta parameter '" + std::string(target_param) +
                    "' was not found in function '" + r_function.name + "'";
          return false;
        }
      }
      for (GLSLFunctionParam &param : r_function.params) {
        if (const GLSLRawParamMeta *function_meta =
                meta_by_key.lookup_ptr(make_glsl_meta_key(r_function.name, param.name)))
        {
          if (!apply_glsl_meta_to_param(*function_meta, param, r_error)) {
            if (!r_error.empty()) {
              r_error = "For parameter '" + param.name + "': " + r_error;
            }
            return false;
          }
        }
      }
      return true;
    }

static void node_declare(NodeDeclarationBuilder &b)
{
  const bNode *node = b.node_or_null();
  if (node == nullptr) {
    return;
  }

  const std::string source = glslfn_read_source(node);
  Vector<GLSLToken> tokens = tokenize_glsl_source(source, true);
  Vector<std::string> names = find_top_level_glsl_function_names(tokens);
  const std::string chosen = glslfn_pick_function(node, names);
  GLSLFunctionDefinition func;
  std::string error;
  bool parsed = false;
  if (!chosen.empty()) {
    parsed = find_glsl_function_definition(tokens, chosen, func, error);
  }

  if (parsed) {
    Map<std::string, GLSLRawParamMeta> meta_by_key;
    if (!extract_glsl_meta(source, meta_by_key, error)) {
      meta_by_key.clear();
    }
    apply_glsl_meta_to_function(meta_by_key, func, error);
  }

  if (!parsed) {
    b.add_input<decl::Vector>("Color"_ustr, "In_Color"_ustr);
    b.add_output<decl::Vector>("Color"_ustr, "Result"_ustr);
    return;
  }

  for (const GLSLFunctionParam &param : func.params) {
    if (!glsl_param_has_input_socket(param)) {
      continue;
    }
    const UString socket_name(param.name.c_str());
    const UString socket_id(make_socket_identifier("In", param.name));
    const GLSLFunctionParam::Meta &meta = param.meta;
    switch (param.type) {
      case GLSLBoundaryType::Float: {
        auto &decl = b.add_input<decl::Float>(socket_name, socket_id)
                         .min(meta.has_min ? meta.min_value : -10000.0f)
                         .max(meta.has_max ? meta.max_value : 10000.0f);
        if (meta.has_default_value) {
          decl.default_value(meta.default_value.x);
        }
        if (meta.hide_value) {
          decl.hide_value();
        }
        if (meta.subtype.has_value()) {
          decl.subtype(*meta.subtype);
        }
        if (meta.description.has_value()) {
          decl.description(*meta.description);
        }
        break;
      }
      case GLSLBoundaryType::Int: {
        auto &decl = b.add_input<decl::Int>(socket_name, socket_id)
                         .min(meta.has_min ? meta.int_min_value.value_or(-10000) : -10000)
                         .max(meta.has_max ? meta.int_max_value.value_or(10000) : 10000);
        if (meta.has_default_value) {
          decl.default_value(meta.int_default_value.value_or(0));
        }
        if (meta.hide_value) {
          decl.hide_value();
        }
        if (meta.description.has_value()) {
          decl.description(*meta.description);
        }
        break;
      }
      case GLSLBoundaryType::Bool: {
        auto &decl = b.add_input<decl::Bool>(socket_name, socket_id);
        if (meta.has_default_value) {
          decl.default_value(meta.default_value.x != 0.0f);
        }
        if (meta.hide_value) {
          decl.hide_value();
        }
        if (meta.description.has_value()) {
          decl.description(*meta.description);
        }
        break;
      }
      case GLSLBoundaryType::Vec2:
      case GLSLBoundaryType::Vec3:
      case GLSLBoundaryType::Vec4: {
        const int dim = glsl_boundary_dimensions(param.type);
        auto &decl = b.add_input<decl::Vector>(socket_name, socket_id)
                         .dimensions(dim)
                         .min(meta.has_min ? meta.min_value : -10000.0f)
                         .max(meta.has_max ? meta.max_value : 10000.0f);
        if (meta.has_default_value) {
          switch (dim) {
            case 2:
              decl.default_value(float2(meta.default_value.x, meta.default_value.y));
              break;
            case 3:
              decl.default_value(float3(meta.default_value.x, meta.default_value.y,
                                        meta.default_value.z));
              break;
            case 4:
              decl.default_value(meta.default_value);
              break;
          }
        }
        if (meta.subtype.has_value()) {
          decl.subtype(*meta.subtype);
        }
        if (meta.hide_value) {
          decl.hide_value();
        }
        if (meta.description.has_value()) {
          decl.description(*meta.description);
        }
        break;
      }
      default:
        break;
    }
  }

  if (func.return_type != GLSLBoundaryType::Void &&
      func.return_type != GLSLBoundaryType::Unsupported)
  {
    const UString out_name("Result");
    const UString out_id("Result");
    switch (func.return_type) {
      case GLSLBoundaryType::Float:
        b.add_output<decl::Float>(out_name, out_id);
        break;
      case GLSLBoundaryType::Int:
        b.add_output<decl::Int>(out_name, out_id);
        break;
      case GLSLBoundaryType::Bool:
        b.add_output<decl::Bool>(out_name, out_id);
        break;
      case GLSLBoundaryType::Vec2:
      case GLSLBoundaryType::Vec3:
      case GLSLBoundaryType::Vec4: {
        auto &decl = b.add_output<decl::Vector>(out_name, out_id);
        decl.dimensions(glsl_boundary_dimensions(func.return_type));
        break;
      }
      default:
        break;
    }
  }
}

static void node_init(bNodeTree * /*ntree*/, bNode *node)
{
  node->storage = MEM_new<NodeShaderGLSLFunction>("NodeShaderGLSLFunction");
}

static void node_layout(ui::Layout &layout, bContext * /*C*/, PointerRNA * /*ptr*/)
{
  layout.label("GLSL Function (stage-1 skeleton, not yet functional)", ICON_INFO);
}

/* Minimal prototype: hardcoded GLSL source, no dynamic parsing.
 * Verifies the pipeline: generated_source_add -> GPU_stack_link_custom
 * -> EEVEE compile -> render. */
/* Minimal GLSL Function: fixed vec3->vec3 signature.
 * Source comes from the node's Text data block (fallback to a built-in
 * default). Body is passed through verbatim. No @glsl_meta parsing,
 * no sampler, no closure, no light access. */

static int node_shader_gpu_glsl_function(GPUMaterial *mat,
                                         bNode *node,
                                         bNodeExecData * /*execdata*/,
                                         GPUNodeStack *in,
                                         GPUNodeStack *out)
{
  const std::string source = glslfn_read_source(node);

  Vector<GLSLToken> tokens = tokenize_glsl_source(source, true);
  Vector<std::string> names = find_top_level_glsl_function_names(tokens);
  const std::string chosen = glslfn_pick_function(node, names);
  GLSLFunctionDefinition func;
  std::string error;
  bool parsed = false;
  if (!chosen.empty()) {
    parsed = find_glsl_function_definition(tokens, chosen, func, error);
  }

  const std::string wrapper_filename = "__glslfn_wrap.glsl";
  const std::string wrapper_name = "glslfn_wrap_node";
  std::string wrapper;

  if (parsed) {
    std::string params;
    std::string args;
    for (const GLSLFunctionParam &param : func.params) {
      if (!glsl_param_has_input_socket(param)) {
        continue;
      }
      const std::string id = make_socket_identifier("In", param.name);
      if (!params.empty()) {
        params += ", ";
        args += ", ";
      }
      params += param.type_name + " " + id;
      args += id;
    }
    /* Blender shader functions use the out-parameter convention:
     * void fn(in..., out T result). The codegen always passes the node's
     * output socket as the final argument. */
    const bool has_return = (func.return_type != GLSLBoundaryType::Void);
    if (has_return) {
      if (!params.empty()) {
        params += ", ";
      }
      params += "out " + func.return_type_name + " Result";
    }
    wrapper = "void " + wrapper_name + "(" + params + ")\n{\n";
    if (has_return) {
      wrapper += "  Result = " + func.name + "(" + args + ");\n";
    } else {
      wrapper += "  " + func.name + "(" + args + ");\n";
    }
    wrapper += "}\n";
  } else {
    wrapper = "vec3 " + wrapper_name + "(vec3 In_Color)\n{\n  return vec3(0.5);\n}\n";
  }

  std::string combined = source + "\n" + wrapper;
  fprintf(stderr, "[GLSLFN combined]\n%s\n", combined.c_str());
  GPU_material_generated_source_add(mat, wrapper_filename.c_str(), {}, combined.c_str());

  return GPU_stack_link_custom(mat,
                               node,
                               wrapper_name.c_str(),
                               wrapper_filename.c_str(),
                               GPU_CUSTOM_NODE_DEPENDENCY_NONE,
                               in,
                               out)
             ? 1
             : 0;
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
