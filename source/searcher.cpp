#include "searcher.hpp"

#include "lexer.hpp"
#include "strstr.hpp"

#include <clang-c/Index.h> // This is libclang.
#include <fnmatch.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <string_view>

namespace fs = std::filesystem;
using namespace std::string_view_literals;

namespace fccf {

namespace {

void print_code_snippet(std::string_view filename, bool is_stdout,
                        unsigned start_line, unsigned end_line,
                        std::string_view code_snippet) {
  auto out = fmt::memory_buffer();
  if (is_stdout) {
    fmt::format_to(std::back_inserter(out), "\n\033[1;90m// {}\033[0m ",
                   filename);
  } else {
    fmt::format_to(std::back_inserter(out), "\n// {} ", filename);
  }

  if (is_stdout) {
    fmt::format_to(std::back_inserter(out),
                   "\033[1;90m(Line: {} to {})\033[0m\n", start_line, end_line);
  } else {
    fmt::format_to(std::back_inserter(out), "(Line: {} to {})\n", start_line,
                   end_line);
  }
  lexer lex;
  lex.tokenize_and_pretty_print(code_snippet, &out, is_stdout);
  fmt::format_to(std::back_inserter(out), "\n");
  fmt::print("{}", fmt::to_string(out));
}

bool exclude_directory(const char *path) {
  static const std::array<const char *, 34> ignored_dirs = {
      ".git/",         ".github/",       "build/",
      "node_modules/", ".vscode/",       ".DS_Store/",
      "debugPublic/",  "DebugPublic/",   "debug/",
      "Debug/",        "Release/",       "release/",
      "Releases/",     "releases/",      "cmake-build-debug/",
      "__pycache__/",  "Binaries/",      "Doc/",
      "doc/",          "Documentation/", "docs/",
      "Docs/",         "bin/",           "Bin/",
      "patches/",      "tar-install/",   "CMakeFiles/",
      "install/",      "snap/",          "LICENSES/",
      "img/",          "images/",        "imgs/",
      ".cache/"};

  return std::any_of(ignored_dirs.cbegin(), ignored_dirs.cend(),
                     [path](const char *ignored_dir) -> bool {
                       return strstr(path, ignored_dir) != nullptr;
                     });
}

struct client_args {
  fccf::searcher *searcher;
  std::string_view filename;
  std::string_view haystack;
};

bool is_hit(searcher *searcher, CXCursor cursor) {
  return (searcher->m_search_expressions &&
          (cursor.kind == CXCursor_DeclRefExpr ||
           cursor.kind == CXCursor_MemberRefExpr ||
           cursor.kind == CXCursor_MemberRef ||
           cursor.kind == CXCursor_FieldDecl)) ||
         (searcher->m_search_for_enum && cursor.kind == CXCursor_EnumDecl) ||
         (searcher->m_search_for_struct &&
          cursor.kind == CXCursor_StructDecl) ||
         (searcher->m_search_for_union && cursor.kind == CXCursor_UnionDecl) ||
         (searcher->m_search_for_member_function &&
          cursor.kind == CXCursor_CXXMethod) ||
         (searcher->m_search_for_function &&
          cursor.kind == CXCursor_FunctionDecl) ||
         (searcher->m_search_for_function_template &&
          cursor.kind == CXCursor_FunctionTemplate) ||
         (searcher->m_search_for_class && cursor.kind == CXCursor_ClassDecl) ||
         (searcher->m_search_for_class_template &&
          cursor.kind == CXCursor_ClassTemplate) ||
         (searcher->m_search_for_class_constructor &&
          cursor.kind == CXCursor_Constructor) ||
         (searcher->m_search_for_class_destructor &&
          cursor.kind == CXCursor_Destructor) ||
         (searcher->m_search_for_typedef &&
          cursor.kind == CXCursor_TypedefDecl) ||
         (searcher->m_search_for_using_declaration &&
          (cursor.kind == CXCursor_UsingDirective ||
           cursor.kind == CXCursor_UsingDeclaration ||
           cursor.kind == CXCursor_TypeAliasDecl)) ||
         (searcher->m_search_for_namespace_alias &&
          cursor.kind == CXCursor_NamespaceAlias) ||
         (searcher->m_search_for_variable_declaration &&
          cursor.kind == CXCursor_VarDecl) ||
         (searcher->m_search_for_parameter_declaration &&
          cursor.kind == CXCursor_ParmDecl) ||
         (searcher->m_search_for_static_cast &&
          cursor.kind == CXCursor_CXXStaticCastExpr) ||
         (searcher->m_search_for_dynamic_cast &&
          cursor.kind == CXCursor_CXXDynamicCastExpr) ||
         (searcher->m_search_for_reinterpret_cast &&
          cursor.kind == CXCursor_CXXReinterpretCastExpr) ||
         (searcher->m_search_for_const_cast &&
          cursor.kind == CXCursor_CXXConstCastExpr) ||
         (searcher->m_search_for_throw_expression &&
          cursor.kind == CXCursor_CXXThrowExpr) ||
         (searcher->m_search_for_for_statement &&
          (cursor.kind == CXCursor_ForStmt ||
           cursor.kind == CXCursor_CXXForRangeStmt));
}

CXChildVisitResult visitor(CXCursor cursor, CXCursor /*parent*/,
                           CXClientData client_data) {
  auto *args = static_cast<client_args *>(client_data);
  auto *searcher = args->searcher;
  auto filename = args->filename;
  auto haystack = args->haystack;

  if (is_hit(searcher, cursor)) {
    auto source_range = clang_getCursorExtent(cursor);
    auto start_location = clang_getRangeStart(source_range);
    auto end_location = clang_getRangeEnd(source_range);

    CXFile file = nullptr;
    unsigned start_line = 0;
    unsigned start_column = 0;
    unsigned start_offset = 0;
    clang_getExpansionLocation(start_location, &file, &start_line,
                               &start_column, &start_offset);

    unsigned end_line = 0;
    unsigned end_column = 0;
    unsigned end_offset = 0;
    clang_getExpansionLocation(end_location, &file, &end_line, &end_column,
                               &end_offset);

    if ((!searcher->m_ignore_single_line_results && end_line >= start_line) ||
        (searcher->m_ignore_single_line_results && end_line > start_line)) {
      std::string_view name =
          static_cast<const char *>(clang_getCursorSpelling(cursor).data);
      std::string_view query = searcher->m_query;

      if (query.empty() ||
          (
              // The query check for these is done
              // a little later down the road
              // (once a code snippet is available
              // to check against)
              searcher->m_search_for_throw_expression ||
              searcher->m_search_for_typedef ||
              searcher->m_search_for_static_cast ||
              searcher->m_search_for_dynamic_cast ||
              searcher->m_search_for_reinterpret_cast ||
              searcher->m_search_for_const_cast ||
              searcher->m_search_for_for_statement) ||
          (searcher->m_exact_match && name == query &&
           cursor.kind != CXCursor_DeclRefExpr &&
           cursor.kind != CXCursor_MemberRefExpr &&
           cursor.kind != CXCursor_MemberRef &&
           cursor.kind != CXCursor_FieldDecl) ||
          (!searcher->m_exact_match &&
           name.find(query) != std::string_view::npos)) {
        auto haystack_size = haystack.size();
        auto pos = source_range.begin_int_data - 2;
        auto count = source_range.end_int_data - source_range.begin_int_data;

        // fmt::print("{} - Pos: {}, Count: {}, Haystack size:
        // {}\n", filename, pos, count, haystack_size);

        if ((searcher->m_search_expressions &&
             (cursor.kind == CXCursor_DeclRefExpr ||
              cursor.kind == CXCursor_MemberRefExpr ||
              cursor.kind == CXCursor_MemberRef ||
              cursor.kind == CXCursor_FieldDecl))) {
          // Update pos and count so that the entire line of code is
          // printed instead of just the reference (e.g., variable
          // name)
          auto newline_before = haystack.rfind('\n', pos);
          while (haystack[newline_before + 1] == ' ' ||
                 haystack[newline_before + 1] == '\t') {
            newline_before += 1;
          }
          auto newline_after = haystack.find('\n', pos);
          pos = newline_before + 1;
          count = newline_after - newline_before - 1;
        }

        if (pos < haystack_size) {
          auto code_snippet = haystack.substr(pos, count);

          // Handles throw expression, static_cast,
          // dynamic_cast, const_cast, reinterpret_cast
          // for_statement, and ranged_for_statement
          //
          // if the `query` is part of the code snippet,
          // then show result, else, skip it
          if (searcher->m_search_for_throw_expression ||
              searcher->m_search_for_typedef ||
              searcher->m_search_for_static_cast ||
              searcher->m_search_for_dynamic_cast ||
              searcher->m_search_for_reinterpret_cast ||
              searcher->m_search_for_const_cast ||
              searcher->m_search_for_for_statement) {
            if (code_snippet.find(query) == std::string_view::npos) {
              // skip result
              return CXChildVisit_Continue;
            }
          }
          if (searcher->m_custom_printer) {
            searcher->m_custom_printer(filename, searcher->m_is_stdout,
                                       start_line, end_line, code_snippet);
          } else {
            print_code_snippet(filename, searcher->m_is_stdout, start_line,
                               end_line, code_snippet);
          }

          // Line number (start, end)
        }
      }
    }
  }
  return CXChildVisit_Recurse;
}

bool is_whitelisted(std::string_view str) {
  static const std::array<std::string_view, 11> allowed_suffixes{
      // C
      ".c"sv, ".h"sv,
      // C++
      ".cpp"sv, ".cc"sv, ".cxx"sv, ".hh"sv, ".hxx"sv, ".hpp"sv, ".ixx"sv,
      // CUDA
      ".cu"sv, ".cuh"sv};

  return std::any_of(allowed_suffixes.cbegin(), allowed_suffixes.cend(),
                     [str](const auto suffix) -> bool {
                       return std::equal(suffix.rbegin(), suffix.rend(),
                                         str.rbegin());
                     });
}

std::string_view::const_iterator
needle_search(std::string_view needle,
              std::string_view::const_iterator haystack_begin,
              std::string_view::const_iterator haystack_end) {
  if (haystack_begin != haystack_end) {
    return std::search(haystack_begin, haystack_end, needle.begin(),
                       needle.end());
  }
  return haystack_end;
}

std::string get_file_contents(const char *filename) {
  std::FILE *fp = std::fopen(filename, "rb");
  if (fp) {
    std::string contents;
    std::fseek(fp, 0, SEEK_END);
    contents.resize(std::ftell(fp));
    std::rewind(fp);
    const auto size = std::fread(contents.data(), 1, contents.size(), fp);
    std::fclose(fp);
    return (contents);
  }
  return "";
}

} // namespace

void searcher::file_search(std::string_view filename,
                           std::string_view haystack) {
  // Start from the beginning
  const auto *haystack_begin = haystack.cbegin();
  const auto *haystack_end = haystack.cend();

  const auto *ptr = haystack_begin;
  bool first_search = true;
  bool printed_file_name = false;
  std::size_t current_line_number = 1;
  auto no_file_name = filename.empty();

#if defined(HAVE_SIMD_STRSTR)
  std::string_view view(ptr, haystack_end - ptr);
  if (view.empty()) {
    ptr = haystack_end;
  } else {
    auto pos = STRSTR_IMPL(std::string_view(ptr, haystack_end - ptr), m_query);
    if (pos != std::string::npos) {
      ptr += pos;
    } else {
      ptr = haystack_end;
    }
  }
#else
  it = needle_search(m_query, it, haystack_end);
#endif

  if (ptr != haystack_end) {
    // analyze file
    const char *path = filename.data();
    if (m_verbose) {
      fmt::print("Checking {}\n", path);
    }

    // Update a copy of the clang options
    // Include
    auto clang_options = m_clang_options;
    auto parent_path = fs::path(filename).parent_path();
    auto parent_path_str = "-I" + parent_path.string();
    auto grandparent_path = parent_path.parent_path();
    auto grandparent_path_str = "-I" + grandparent_path.string();
    clang_options.push_back(parent_path_str.c_str());
    clang_options.push_back(grandparent_path_str.c_str());
    clang_options.push_back("-I/usr/include");
    clang_options.push_back("-I/usr/local/include");

    if (m_verbose) {
      fmt::print("Clang options:\n");
      for (auto &option : clang_options) {
        fmt::print("{} ", option);
      }
      fmt::print("\n");
    }

    CXIndex index = nullptr;
    if (m_verbose) {
      index = clang_createIndex(0, 1);
    } else {
      index = clang_createIndex(0, 0);
    }
    CXTranslationUnit unit = clang_parseTranslationUnit(
        index, path, clang_options.data(), clang_options.size(), nullptr, 0,
        CXTranslationUnit_KeepGoing |
            CXTranslationUnit_IgnoreNonErrorsFromIncludedFiles);
    // CXTranslationUnit_None);
    if (unit == nullptr) {
      fmt::print("Error: Unable to parse translation unit {}. Quitting.\n",
                 path);
      std::exit(-1);
    }

    CXCursor cursor = clang_getTranslationUnitCursor(unit);

    client_args args = {this, filename, haystack};

    if (clang_visitChildren(cursor, visitor, (void *)(&args)) > 0) {
      fmt::print("Error: Visit children failed for {}\n)", path);
    }

    clang_disposeTranslationUnit(unit);
    clang_disposeIndex(index);
  }
}

void searcher::read_file_and_search(const char *path) {
  const std::string haystack = get_file_contents(path);
  file_search(path, haystack);
}

void searcher::directory_search(const char *search_path) {
  static const bool skip_fnmatch =
      m_filters.size() == 1 && m_filters[0] == "*.*"sv;

  for (auto const &dir_entry : fs::recursive_directory_iterator(search_path)) {
    const auto &path = dir_entry.path();
    const char *path_string = path.c_str();
    if ((m_no_ignore_dirs || !exclude_directory(path_string)) &&
        fs::is_regular_file(path)) {
      const auto glob_match = [path_string](const auto glob) -> bool {
        return fnmatch(glob.data(), path_string, 0) == 0;
      };

      const bool consider_file =
          (skip_fnmatch && is_whitelisted(path_string)) ||
          (!skip_fnmatch &&
           std::any_of(m_filters.cbegin(), m_filters.cend(), glob_match) &&
           (m_excludes.empty() ||
            std::none_of(m_excludes.cbegin(), m_excludes.cend(), glob_match)));

      if (consider_file) {
        m_ts->push_task([this, pathstring = std::string{path_string}]() {
          read_file_and_search(pathstring.data());
        });
      }
    }
  }
  m_ts->wait_for_tasks();
}

} // namespace fccf
