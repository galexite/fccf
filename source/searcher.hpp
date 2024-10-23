#pragma once
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#define FMT_HEADER_ONLY 1
#include <fmt/color.h>
#include <fmt/core.h>
#include <thread_pool.hpp>

namespace fccf {

using custom_printer_callback = std::function<void(
    std::string_view filename, bool is_stdout, unsigned start_line,
    unsigned end_line, std::string_view code_snippet)>;

struct searcher {
  std::unique_ptr<thread_pool> m_ts;
  std::string_view m_query;
  std::vector<std::string> m_filters;
  std::vector<std::string> m_excludes;
  bool m_no_ignore_dirs;
  bool m_verbose;
  bool m_is_stdout;
  std::vector<const char *> m_clang_options;
  bool m_exact_match;
  bool m_search_for_enum;
  bool m_search_for_struct;
  bool m_search_for_union;
  bool m_search_for_member_function;
  bool m_search_for_function;
  bool m_search_for_function_template;
  bool m_search_for_class;
  bool m_search_for_class_template;
  bool m_search_for_class_constructor;
  bool m_search_for_class_destructor;
  bool m_search_for_typedef;
  bool m_search_for_using_declaration;
  bool m_search_for_namespace_alias;
  bool m_ignore_single_line_results;
  bool m_search_expressions;
  bool m_search_for_variable_declaration;
  bool m_search_for_parameter_declaration;
  bool m_search_for_static_cast;
  bool m_search_for_dynamic_cast;
  bool m_search_for_reinterpret_cast;
  bool m_search_for_const_cast;
  bool m_search_for_throw_expression;
  bool m_search_for_for_statement;
  custom_printer_callback m_custom_printer;

  void file_search(std::string_view filename, std::string_view haystack);
  void read_file_and_search(const char *path);
  void directory_search(const char *search_path);
};

} // namespace search
