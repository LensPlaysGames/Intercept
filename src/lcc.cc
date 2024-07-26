#include <cli.hh>

#include <lcc/context.hh>
#include <lcc/diags.hh>
#include <lcc/format.hh>
#include <lcc/ir/module.hh>
#include <lcc/lcc-c.h>
#include <lcc/opt/opt.hh>
#include <lcc/target.hh>
#include <lcc/utils.hh>
#include <lcc/utils/platform.hh>

// FIXME: We should probably have a more unified API for each language
#include <glint/ast.hh>
#include <glint/ir_gen.hh>
#include <glint/parser.hh>
#include <glint/sema.hh>
#include <intercept/ast.hh>
#include <intercept/ir_gen.hh>
#include <intercept/parser.hh>
#include <intercept/sema.hh>

#include <cstdio>  // fopen and friends
#include <cstdlib> // system
#include <cstring> // strerror
#include <filesystem>
#include <fmt/format.h>
#include <format>
#include <string>
#include <string_view>
#include <vector>

void aluminium_handler() {
#if defined(LCC_PLATFORM_WINDOWS)
    // Windows
    std::system("start https://www.youtube.com/watch?v=dQw4w9WgXcQ");
#elif defined(__APPLE__)
    // Apple
    std::system("open https://www.youtube.com/watch?v=dQw4w9WgXcQ");
#elif __linux__ || __unix__
    // Linux-ish
    std::system("xdg-open https://www.youtube.com/watch?v=dQw4w9WgXcQ");
#endif
}

/// Default target.
const lcc::Target* const default_target =
#if defined(LCC_PLATFORM_WINDOWS)
    lcc::Target::x86_64_windows;
#elif defined(__APPLE__) or defined(__linux__)
    lcc::Target::x86_64_linux;
#else
#    error "Unsupported target"
#endif

/// Default format
const lcc::Format* const default_format = lcc::Format::gnu_as_att_assembly;

auto main(int argc, const char** argv) -> int {
    auto options = cli::parse(argc, argv);

    if (options.aluminium) {
        aluminium_handler();
        return 0;
    }

    /// Determine whether to use colours in the output.
    /// TODO: Enable colours in the console on Windows (for `cmd`).
    auto colour_opt = options.color;
    bool use_colour{};
    if (colour_opt == "always") use_colour = true;
    else if (colour_opt == "never") use_colour = false;
    else use_colour = lcc::platform::StdoutIsTerminal() or lcc::platform::StderrIsTerminal();

    /// Get input files
    auto& input_files = options.input_files;
    if (options.verbose) {
        fmt::print("Input files:\n");
        for (const auto& input_file : input_files)
            fmt::print("- {}\n", input_file);
    }

    if (input_files.empty())
        lcc::Diag::Fatal("no input files");

    /// Compile the file.

    // Get format from command line option, falling back to default.
    // TODO: Get target from "-t" or "--target" command line option.
    auto* format = default_format;
    if (options.format == "default") {
        ;
    } else if (options.format == "ir" or options.format == "IR") {
        format = lcc::Format::lcc_ir;
    } else if (options.format == "asm" || options.format == "gnu-as-att") {
        format = lcc::Format::gnu_as_att_assembly;
    } else if (options.format == "obj") {
#if defined(_MSC_VER)
        format = lcc::Format::coff_object;
#else
        format = lcc::Format::elf_object;
#endif
    } else if (options.format == "elf") {
        format = lcc::Format::elf_object;
    } else if (options.format == "coff") {
        format = lcc::Format::coff_object;
    } else if (options.format == "llvm") {
        format = lcc::Format::llvm_textual_ir;
    } else LCC_ASSERT(false, "Unhandled format");

    lcc::Context context{
        default_target,
        format,
        use_colour,
        options.mir,
        options.stopat_mir //
    };

    context.add_include_directory(".");
    for (const auto& dir : options.include_directories) {
        if (options.verbose) fmt::print("Added input directory: {}\n", dir);
        context.add_include_directory(dir);
    }

    auto ConvertFileExtensionToOutputFormat = [&](const std::string& path_string) {
        const char* replacement = ".s";
        if (context.format()->format() == lcc::Format::LLVM_TEXTUAL_IR)
            replacement = ".ll";
        if (context.format()->format() == lcc::Format::ELF_OBJECT or context.format()->format() == lcc::Format::COFF_OBJECT)
            replacement = ".o";

        return std::filesystem::path{path_string}
            .replace_extension(replacement)
            .string();
    };

    /// Common path after IR gen.
    auto EmitModule = [&](lcc::Module* m, std::string_view input_file_path, std::string_view output_file_path) {
        if (not options.optimisation_passes.empty()) {
            lcc::opt::RunPasses(m, options.optimisation_passes);
            if (options.ir) m->print_ir(use_colour);
        }

        if (options.ir)
            m->print_ir(use_colour);

        // NOTE: Only apply full optimisation if specific passes were not requested.
        if (options.optimisation and options.optimisation_passes.empty())
            lcc::opt::Optimise(m, int(options.optimisation));

        if (options.ir) {
            fmt::print("\nAfter Optimisations:\n");
            m->print_ir(use_colour);
        }

        m->lower();

        if (options.ir) {
            fmt::print("\nAfter Lowering:\n");
            m->print_ir(use_colour);
        }

        if (options.stopat_ir) return;

        m->emit(output_file_path);

        if (options.verbose)
            fmt::print("Generated output from {} at {}\n", input_file_path, output_file_path);
    };

    // NOTE: Moves the input file, so, uhh, don't use that after passing it to
    // this.
    auto specified_language = options.language;
    auto GenerateOutputFile = [&](std::string& input_file, std::string_view output_file_path) {
        std::filesystem::path path{input_file};
        // Don't use input_file since it may be moved from.
        auto path_str = path.lexically_normal().string();

        if (not std::filesystem::exists(path)) {
            lcc::Diag::Error(
                "Input file does not exist: {}",
                path.lexically_normal().string()
            );
            return;
        }
        if (not std::filesystem::is_regular_file(path)) {
            lcc::Diag::Error(
                "Input file exists, but is not a regular file: {}",
                path.lexically_normal().string()
            );
            return;
        }

        auto* f = fopen(path.string().data(), "rb");
        if (not f) {
            lcc::Diag::Fatal(
                "Could not open file at {}: {}",
                path.lexically_normal().string(),
                std::strerror(errno)
            );
            return;
        }

        // Get size of file, restoring cursor to beginning.
        fseek(f, 0, SEEK_END);
        size_t fsize = size_t(ftell(f));
        fseek(f, 0, SEEK_SET);

        std::vector<char> contents{};
        contents.resize(fsize);
        auto nread = fread(contents.data(), 1, fsize, f);
        if (nread != fsize) {
            fclose(f);
            lcc::Diag::Fatal(
                "ERROR reading file {}\n"
                "    Got {} bytes, expected {}\n",
                path.lexically_normal().string(),
                nread,
                fsize
            );
            LCC_UNREACHABLE();
        }
        fclose(f);

        auto& file = context.create_file(
            std::move(input_file),
            std::move(contents)
        );

        /// LCC IR.
        if (specified_language == "ir" or (specified_language == "default" and path_str.ends_with(".lcc"))) {
            auto mod = lcc::Module::Parse(&context, file);
            if (context.has_error()) return; // the error condition is handled by the caller already
            return EmitModule(mod.get(), path_str, output_file_path);
        }

        /// Glint.
        if (specified_language == "glint" or (specified_language == "default" and path_str.ends_with(".g"))) {
            // Parse the file.
            auto mod = lcc::glint::Parser::Parse(&context, file);
            if (options.ast and mod) mod->print(use_colour);
            // The error condition is handled by the caller already.
            if (context.has_error()) return;
            if (options.stopat_syntax) return;

            // Perform semantic analysis.
            lcc::glint::Sema::Analyse(
                &context,
                *mod,
                context.use_colour_diagnostics()
            );
            if (options.ast) {
                fmt::print("\nAfter Sema:\n");
                mod->print(use_colour);
            }
            // The error condition is handled by the caller already.
            if (context.has_error()) return;
            // Stop after sema if requested.
            if (options.stopat_sema) return;

            auto* ir = lcc::glint::IRGen::Generate(&context, *mod);
            if (context.has_error()) return;

            return EmitModule(ir, path_str, output_file_path);
        }

        /// Intercept.
        if (specified_language == "int" or (specified_language == "default" and path_str.ends_with(".int"))) {
            /// Parse the file.
            auto mod = lcc::intercept::Parser::Parse(&context, file);
            if (context.has_error()) return; // the error condition is handled by the caller already
            if (options.ast) mod->print(use_colour);
            if (options.stopat_syntax) return;

            /// Perform semantic analysis.
            lcc::intercept::Sema::Analyse(&context, *mod, true);
            if (context.has_error()) return; // the error condition is handled by the caller already
            if (options.ast) mod->print(use_colour);
            if (options.stopat_sema) return;

            return EmitModule(lcc::intercept::IRGen::Generate(&context, *mod), path_str, output_file_path);
        }

        lcc::Diag::Fatal("Unrecognised input file type");
    };

    auto configured_output_file_path = options.output_filepath;
    if (input_files.size() == 1) {
        std::string output_file_path = configured_output_file_path;
        if (output_file_path.empty())
            output_file_path = ConvertFileExtensionToOutputFormat(input_files[0]);

        GenerateOutputFile(input_files[0], output_file_path);
        if (context.has_error()) return 1;
        if (options.verbose) fmt::print("Generated output at {}\n", output_file_path);
    } else {
        if (not configured_output_file_path.empty()) {
            lcc::Diag::Fatal(
                "Cannot specify -o when generating multiple output files (would overwrite the same file with every output).\n"
                "If you have a suggestion of how you think this should behave, let a developer know.\n"
            );
        }

        for (auto& input_file : input_files) {
            std::string input_file_path = input_file;
            std::string output_file_path = ConvertFileExtensionToOutputFormat(input_file_path);
            GenerateOutputFile(input_file, output_file_path);
        }
    }

    return 0;
}
