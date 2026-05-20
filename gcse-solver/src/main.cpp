#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

#include "executor.hpp"
#include "llm_engine.hpp"
#include "output_formatter.hpp"
#include "tag_extractor.hpp"

namespace {

struct CliArgs {
    std::string model_path;
    std::string problem;
    bool        repl     = false;
    bool        verbose  = false;
    bool        help     = false;
    bool        bad_args = false;
};

volatile std::sig_atomic_t g_interrupted = 0;

void on_sigint(int /*sig*/) { g_interrupted = 1; }

void print_usage(const char* program_name) {
    std::fprintf(stderr,
                 "Usage: %s --model <path> [--repl] [--verbose] [problem]\n"
                 "\n"
                 "  --model <path>   Path to GGUF model file (required)\n"
                 "  --repl           Interactive read-eval-print loop\n"
                 "  --verbose        Log diagnostics to stderr\n"
                 "  -h, --help       Show this help\n"
                 "\n"
                 "Examples:\n"
                 "  %s --model model.gguf \"Solve 3x + 7 = 22\"\n"
                 "  %s --model model.gguf --repl\n",
                 program_name, program_name, program_name);
}

[[nodiscard]] CliArgs parse_args(int argc, char* argv[]) {
    CliArgs args;
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "-h" || a == "--help") {
            args.help = true;
        } else if (a == "--model") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--model requires a path argument\n");
                args.bad_args = true;
                break;
            }
            args.model_path = argv[++i];
        } else if (a == "--repl") {
            args.repl = true;
        } else if (a == "--verbose") {
            args.verbose = true;
        } else if (!a.empty() && a.front() == '-') {
            std::fprintf(stderr, "unknown flag: %s\n", argv[i]);
            args.bad_args = true;
            break;
        } else {
            if (!args.problem.empty()) args.problem.push_back(' ');
            args.problem.append(a);
        }
    }
    return args;
}

void solve_one(LLMEngine& engine, ExecutorEngine& executor, std::string_view problem,
               bool verbose) {
    const std::string raw      = engine.query(problem);
    const auto        extracted = tag_extractor::extract_calc_block(raw);

    if (!extracted) {
        if (verbose) std::fprintf(stderr, "[main] no [CALC] block found in output\n");
        output_formatter::print_error(problem, raw, "missing [CALC]...[/CALC] in LLM output");
        return;
    }

    std::string    code   = *extracted;
    ExecutorResult result = executor.execute(code);

    if (!result.success) {
        if (verbose) std::fprintf(stderr, "[main] retrying with error context: %s\n",
                                  result.error.c_str());
        const std::string retry_raw       = engine.query_with_error(problem, code, result.error);
        const auto        retry_extracted = tag_extractor::extract_calc_block(retry_raw);
        if (retry_extracted) {
            code   = *retry_extracted;
            result = executor.execute(code);
        }
    }

    if (result.success) {
        output_formatter::print_result(problem, code, result.value);
    } else {
        output_formatter::print_error(problem, code, result.error);
    }
}

void run_repl_mode(LLMEngine& engine, ExecutorEngine& executor, bool verbose) {
    std::fprintf(stderr, "GCSE math solver — REPL mode. Ctrl+D or empty line to exit.\n");
    std::string line;
    while (!g_interrupted) {
        std::fprintf(stderr, "> ");
        std::fflush(stderr);
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) break;
        solve_one(engine, executor, line, verbose);
    }
    std::fprintf(stderr, "exiting REPL\n");
}

[[nodiscard]] std::string read_stdin_problem() {
    std::string out;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!out.empty()) out.push_back(' ');
        out.append(line);
    }
    return out;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::signal(SIGINT, on_sigint);

    const CliArgs args = parse_args(argc, argv);
    if (args.help) {
        print_usage(argv[0]);
        return 0;
    }
    if (args.bad_args) {
        print_usage(argv[0]);
        return 1;
    }
    if (args.model_path.empty()) {
        std::fprintf(stderr, "error: --model is required\n");
        print_usage(argv[0]);
        return 1;
    }

    LLMEngine engine(args.model_path, args.verbose);
    if (!engine.is_initialized()) {
        std::fprintf(stderr, "error: failed to initialize LLM engine\n");
        return 2;
    }
    ExecutorEngine executor;

    if (args.repl) {
        run_repl_mode(engine, executor, args.verbose);
        return 0;
    }

    std::string problem = args.problem;
    if (problem.empty()) {
        problem = read_stdin_problem();
    }
    if (problem.empty()) {
        std::fprintf(stderr, "error: no problem provided (argv or stdin)\n");
        return 1;
    }

    solve_one(engine, executor, problem, args.verbose);
    return 0;
}
