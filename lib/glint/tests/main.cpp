#include <lcc/context.hh>
#include <lcc/format.hh>
#include <lcc/target.hh>
#include <lcc/utils.hh>

#include <glint/ast.hh>
#include <glint/parser.hh>
#include <glint/sema.hh>

#include <filesystem>

static lcc::utils::Colours C{true};
using lcc::utils::Colour;

// TODO: Use something like this for stat-trak language testing framework
class TestContext {
    size_t _count;
    size_t _count_failed;

public:
    size_t count() const { return _count; }
    size_t count_failed() const { return _count_failed; }
    size_t count_passed() const { return _count - _count_failed; }

    void record_test(bool passed = true) {
        ++_count;
        if (not passed) ++_count_failed;
    }
};

/// Default target.
const lcc::Target* default_target =
#if defined(LCC_PLATFORM_WINDOWS)
    lcc::Target::x86_64_windows;
#elif defined(__APPLE__) or defined(__linux__)
    lcc::Target::x86_64_linux;
#else
#    error "Unsupported target"
#endif

/// Default format
const lcc::Format* default_format = lcc::Format::gnu_as_att_assembly;

struct MatchTree {
    std::string_view name;
    std::vector<MatchTree> children{};

    [[nodiscard]]
    std::string print() {
        std::string out{};
        out += fmt::format("({}", name);
        for (auto child : children) {
            out += ' ';
            out += child.print();
        }
        out += ')';
        return out;
    }
};

/// NOTE: print_node() constructs a matcher from a given AST; may be good to
/// utilise this functionality to create initial expected output of test.
[[nodiscard]]
std::string print_node(lcc::glint::Expr* e) {
    std::string out{};
    out += fmt::format("({}", ToString(e->kind()));
    auto children = e->children();
    for (auto* child : children) {
        out += ' ';
        out += print_node(child);
    }
    out += ')';
    return out;
}

bool perform_match(lcc::glint::Expr* e, MatchTree& t) {
    auto name = ToString(e->kind());
    if (name != t.name) {
        // TODO: Record test failure, somewhere/somehow
        fmt::print("\nMISMATCH: node name\n");
        fmt::print("Expected {} but got {}\n", t.name, name);
        return false;
    }

    auto children = e->children();
    if (children.size() != t.children.size()) {
        fmt::print("\nMISMATCH: child count\n");
        return false;
    }

    LCC_ASSERT(
        children.size() == t.children.size(),
        "Child count of expression and matcher must be the same, otherwise the match mustn't be performed."
    );

    bool children_match{true};
    for (size_t i = 0; i < children.size(); ++i) {
        if (not perform_match(children.at(i), t.children.at(i)))
            children_match = false;
    }

    return children_match;
}

void parse_matchtree(std::span<char> contents, const size_t fsize, size_t& i, MatchTree& match) {
    // Get to beginning of list
    while (i < fsize and contents[i] != '(' and contents[i] != '=')
        ++i;
    if (i >= fsize or contents[i] == '=') {
        // TODO: file location
        fmt::print("ERROR nothing expected");
        return;
    }

    // Eat '('
    ++i;

    // Eat whitespace
    while (i < fsize and isspace(contents[i])) ++i;

    if (i >= fsize or contents[i] == '=') {
        // TODO: file location near list opening symbol
        fmt::print("ERROR expected list closing symbol but got end of input");
        return;
    }

    size_t begin_match{i};

    while (i < fsize and not isspace(contents[i]) and not std::string_view("()=-").contains(contents[i]))
        ++i;

    match.name = {
        contents.begin() + lcc::isz(begin_match),
        contents.begin() + lcc::isz(i)};

    // Eat whitespace
    while (i < fsize and isspace(contents[i])) ++i;
    if (i >= fsize or contents[i] == '=') {
        fmt::print("ERROR expected list closing symbol but got end of input");
        return;
    }

    // Eat list closing symbol
    if (contents[i] == ')') {
        ++i;
        return;
    }

    // Parse match children, if necessary
    match.children.push_back({});
    parse_matchtree(contents, fsize, i, match.children.back());
}

struct Test {
    std::string_view name;
    std::string_view source;
    MatchTree matcher;

    void run() {
        LCC_ASSERT(not name.empty(), "Refusing to run test with empty name");

        // Parse source using LCC
        // TODO: Get from context parameter, don't construct our own
        lcc::Context context{
            default_target,
            default_format,
            false,
            false,
            false};

        bool failed_parse{false};
        bool failed_check{false};
        bool matches{true};
        auto mod = lcc::glint::Parser::Parse(&context, source);
        if (not context.has_error()) {
            // Perform type-checking
            lcc::glint::Sema::Analyse(&context, *mod, true);
            if (not context.has_error()) {
                // TODO: Only confirm AST conforms to expected match tree iff test is NOT
                // decorated as expected to fail.

                auto* root = mod->top_level_func()->body();
                matches = perform_match(root, matcher);
            } else failed_check = true;
        } else failed_parse = true;

        fmt::print("{}: ", name);
        if (not matches or failed_parse or failed_check) {
            fmt::print("{}FAIL{}\n", C(Colour::Red), C(Colour::Reset));
            if (not matches) {
                fmt::print("EXPECTED: {}\n", matcher.print());
                fmt::print("GOT: {}\n", print_node(mod->top_level_func()->body()));
            }
        } else fmt::print("{}PASS{}\n", C(Colour::Green), C(Colour::Reset));
    }
};

/// @param[out] test
bool parse_test(std::span<char> contents, const size_t fsize, size_t& i, Test& test) {
    { // Parse test name
        // Skip '=' line
        while (i < fsize and contents[i] != '\n')
            ++i;
        if (i >= fsize) {
            fmt::print("ERROR parsing first line of test\n");
            return false;
        }

        // Eat '\n'
        while (i < fsize and contents[i] == '\n') ++i;

        // Parse test name
        size_t begin{i};
        while (i < fsize and contents[i] != '\n')
            ++i;
        if (i >= fsize) {
            fmt::print("ERROR parsing name of test\n");
            return false;
        }

        test.name = {
            contents.begin() + lcc::isz(begin),
            contents.begin() + lcc::isz(i)};

        // Eat '\n'
        while (i < fsize and contents[i] == '\n') ++i;

        // Skip '=' line
        while (i < fsize and contents[i] != '\n')
            ++i;
        if (i >= fsize) {
            fmt::print("ERROR parsing closing line of name of test {}\n", test.name);
            return false;
        }

        // Eat '\n'
        while (i < fsize and contents[i] == '\n') ++i;
    }

    { // Parse test source
        // Record beginning of source
        size_t begin_source{i};
        // Eat all lines that don't begin with '-', to begin with
        while (i < fsize and contents[i] != '-') {
            // Eat line
            while (i < fsize and contents[i] != '\n')
                ++i;
            // Eat '\n'
            while (i < fsize and contents[i] == '\n') ++i;
        }
        if (i >= fsize) {
            // Got EOF when expected `---` followed by expected test output (AST matcher)
            fmt::print("ERROR parsing source of test {}\n", test.name);
            return false;
        }
        test.source = {
            contents.begin() + lcc::isz(begin_source),
            contents.begin() + lcc::isz(i)};
    }

    { // Parse test expected

        // Skip until start of expectation
        while (i < fsize and contents[i] != '(')
            ++i;
        if (i >= fsize) {
            // Got EOF when expected `(` (beginning of expected test output after `---`)
            fmt::print("ERROR parsing expected of test {}\n", test.name);
            return false;
        }

        parse_matchtree(contents, fsize, i, test.matcher);

        // Skip to beginning of next test or end of input.
        while (i < fsize and contents[i] != '=') ++i;
    }

    return true;
}

void parse_tests(std::span<char> contents) {
    auto fsize = contents.size();
    bool bol = true;
    // Parse tests (first line starting with '=' marks the start of a test)
    for (size_t i = 0; i < fsize; ++i) {
        auto c = contents[i];
        // FIXME: We may want to ensure that the entire line is '=', eventually.
        if (bol and c == '=') {
            Test test{};
            if (parse_test(contents, fsize, i, test))
                test.run();
            bol = true;
        } else bol = c == '\n';
    }
}

void process_ast_test_file(const std::filesystem::path& path) {
    // Read file
    auto path_str = path.string();
    auto f = fopen(path_str.data(), "rb");
    if (not f) {
        fmt::print("ERROR opening file {}\n", path_str);
        return;
    }
    fseek(f, 0, SEEK_END);
    size_t fsize = size_t(ftell(f));
    fseek(f, 0, SEEK_SET);
    std::vector<char> contents{};
    contents.resize(fsize);
    auto nread = fread(contents.data(), 1, fsize, f);
    if (nread != fsize) {
        fmt::print(
            "ERROR reading file {}\n"
            "    Got {} bytes, expected {}\n",
            path_str,
            nread,
            fsize
        );
        fclose(f);
        exit(1);
    }
    fclose(f);

    parse_tests(contents);
}

int main(int argc, const char** argv) {
    for (int i = 1; i < argc; ++i) {
        LCC_ASSERT(false, "No command line options are accepted at the moment");
        // std::string_view arg = argv[i];
    }

    // TODO: Get target from "-t" or "--target" command line option.
    // TODO: Get format from "-f" or "--format" command line option.
    lcc::Context context{
        default_target,
        default_format,
        false,
        false,
        false};

    for (auto entry : std::filesystem::directory_iterator("ast"))
        if (entry.is_regular_file()) process_ast_test_file(entry.path());

    return 0;
}
