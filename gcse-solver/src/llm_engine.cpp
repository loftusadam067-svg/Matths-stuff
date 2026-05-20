#include "llm_engine.hpp"

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "llama.h"

namespace {

constexpr int          kContextSize    = 512;
constexpr int          kBatchSize      = 64;
constexpr int          kMaxNewTokens   = 256;
constexpr float        kSamplerTemp    = 0.0f;  // greedy-equivalent for math determinism
constexpr std::uint32_t kSamplerSeed   = 1u;

constexpr std::string_view kSystemPrompt =
    "You are a GCSE mathematics computation engine with complete mastery of all GCSE math\n"
    "topics: Number, Algebra, Geometry, Trigonometry, Probability, Statistics, and Calculus\n"
    "(Higher tier).\n"
    "\n"
    "You receive GCSE math problems (Foundation, Intermediate, or Higher tier). You must\n"
    "respond ONLY with a single computation block in this exact format:\n"
    "\n"
    "[CALC]\n"
    "<expression or Lua script>\n"
    "[/CALC]\n"
    "\n"
    "RULES FOR ALL RESPONSES:\n"
    "1. Identify the topic (algebra, trig, probability, etc.) and best solution method.\n"
    "2. EXPRESSION FORMAT (single-step):\n"
    "   - One evaluable arithmetic/algebraic expression.\n"
    "   - Example: (-3 + sqrt(9 - 4*2*(-5))) / (2*2)\n"
    "   - Operators: + - * / ^\n"
    "   - Functions: sqrt sin cos tan log exp abs min max floor ceil round\n"
    "3. LUA FORMAT (multi-step):\n"
    "   - Minimal Lua 5.4, must end with: return <answer>\n"
    "   - Functions: math.sqrt math.sin math.cos math.tan math.log math.exp math.pi math.abs\n"
    "   - Degrees -> radians: angle * math.pi / 180\n"
    "4. Variables: single lowercase letters matching the problem (x, y, a, b, r, ...).\n"
    "5. Maintain full floating-point precision; do not round intermediate steps.\n"
    "6. TRICKY CASES:\n"
    "   - Division by zero: detect and return \"ERROR: undefined\".\n"
    "   - Multiple trig solutions in [0,360]: return all, comma-separated.\n"
    "   - sec/cosec/cot: convert to 1/cos, 1/sin, 1/tan.\n"
    "   - Undefined logs/roots: return \"ERROR: <reason>\".\n"
    "7. NO conversational output. Only the [CALC]...[/CALC] block.\n"
    "8. Non-mathematical input: [CALC] return \"NOT_MATH\" [/CALC]\n"
    "9. Unsolvable: [CALC] return \"ERROR: <reason>\" [/CALC]\n";

void log_stderr(bool verbose, const char* fmt, ...) {
    if (!verbose) return;
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
}

}  // namespace

class LLMEngine::Impl {
public:
    Impl(std::string_view model_path, bool verbose) : verbose_(verbose) {
        llama_backend_init();

        llama_model_params mparams = llama_model_default_params();
        mparams.n_gpu_layers       = 0;
        mparams.use_mmap           = true;
        mparams.use_mlock          = false;

        const std::string path(model_path);
        model_ = llama_model_load_from_file(path.c_str(), mparams);
        if (!model_) {
            std::fprintf(stderr, "[llm_engine] failed to load model: %s\n", path.c_str());
            return;
        }

        llama_context_params cparams = llama_context_default_params();
        cparams.n_ctx                = kContextSize;
        cparams.n_batch              = kBatchSize;
        cparams.no_perf              = true;

        ctx_ = llama_init_from_model(model_, cparams);
        if (!ctx_) {
            std::fprintf(stderr, "[llm_engine] failed to create context\n");
            llama_model_free(model_);
            model_ = nullptr;
            return;
        }

        vocab_ = llama_model_get_vocab(model_);
        if (!vocab_) {
            std::fprintf(stderr, "[llm_engine] failed to acquire vocab\n");
            return;
        }

        // Greedy sampler chain for deterministic math output.
        llama_sampler_chain_params sparams = llama_sampler_chain_default_params();
        sampler_                           = llama_sampler_chain_init(sparams);
        llama_sampler_chain_add(sampler_, llama_sampler_init_temp(kSamplerTemp));
        llama_sampler_chain_add(sampler_, llama_sampler_init_dist(kSamplerSeed));

        log_stderr(verbose_, "[llm_engine] loaded model: %s\n", path.c_str());
        initialized_ = true;
    }

    ~Impl() {
        if (sampler_) {
            llama_sampler_free(sampler_);
        }
        if (ctx_) {
            llama_free(ctx_);
        }
        if (model_) {
            llama_model_free(model_);
        }
        llama_backend_free();
    }

    [[nodiscard]] bool is_initialized() const { return initialized_; }

    [[nodiscard]] std::string query(std::string_view problem) {
        if (!initialized_) {
            return "[CALC] return \"ERROR: engine not initialized\" [/CALC]";
        }
        std::string prompt;
        prompt.reserve(kSystemPrompt.size() + problem.size() + 32);
        prompt.append(kSystemPrompt);
        prompt.append("\nProblem: ");
        prompt.append(problem);
        prompt.append("\nOutput:\n");

        return run_inference(prompt);
    }

    [[nodiscard]] std::string query_with_error(std::string_view problem,
                                               std::string_view failed_code,
                                               std::string_view error_msg) {
        if (!initialized_) {
            return "[CALC] return \"ERROR: engine not initialized\" [/CALC]";
        }
        std::string prompt;
        prompt.append(kSystemPrompt);
        prompt.append("\nCORRECTION MODE: the previous [CALC]...[/CALC] failed.\n");
        prompt.append("Previous code: ");
        prompt.append(failed_code);
        prompt.append("\nError: ");
        prompt.append(error_msg);
        prompt.append("\nProblem: ");
        prompt.append(problem);
        prompt.append("\nOutput:\n");

        return run_inference(prompt);
    }

private:
    [[nodiscard]] std::string run_inference(const std::string& prompt) {
        std::vector<llama_token> tokens = tokenize(prompt, /*add_bos=*/true);
        if (tokens.empty()) {
            return "[CALC] return \"ERROR: tokenization failed\" [/CALC]";
        }

        llama_memory_clear(llama_get_memory(ctx_), true);

        llama_batch batch = llama_batch_get_one(tokens.data(), static_cast<int32_t>(tokens.size()));
        if (llama_decode(ctx_, batch) != 0) {
            std::fprintf(stderr, "[llm_engine] prefill decode failed\n");
            return "[CALC] return \"ERROR: decode failed\" [/CALC]";
        }

        std::string        output;
        const llama_token  eos       = llama_vocab_eos(vocab_);
        constexpr std::string_view kStopTag = "[/CALC]";

        for (int generated = 0; generated < kMaxNewTokens; ++generated) {
            llama_token id = llama_sampler_sample(sampler_, ctx_, -1);
            if (id == eos) break;

            output += token_to_piece(id);
            if (output.find(kStopTag) != std::string::npos) break;

            llama_batch step_batch = llama_batch_get_one(&id, 1);
            if (llama_decode(ctx_, step_batch) != 0) {
                std::fprintf(stderr, "[llm_engine] step decode failed at token %d\n", generated);
                break;
            }
            llama_sampler_accept(sampler_, id);
        }

        log_stderr(verbose_, "[llm_engine] generated %zu bytes\n", output.size());
        return output;
    }

    [[nodiscard]] std::vector<llama_token> tokenize(const std::string& text, bool add_bos) {
        std::vector<llama_token> tokens(text.size() + 16);
        const int32_t n = llama_tokenize(vocab_, text.data(), static_cast<int32_t>(text.size()),
                                         tokens.data(), static_cast<int32_t>(tokens.size()),
                                         add_bos, /*parse_special=*/true);
        if (n < 0) {
            tokens.resize(-n);
            const int32_t n2 = llama_tokenize(vocab_, text.data(), static_cast<int32_t>(text.size()),
                                              tokens.data(), static_cast<int32_t>(tokens.size()),
                                              add_bos, /*parse_special=*/true);
            if (n2 < 0) {
                return {};
            }
            tokens.resize(n2);
        } else {
            tokens.resize(n);
        }
        return tokens;
    }

    [[nodiscard]] std::string token_to_piece(llama_token id) {
        std::array<char, 256> buf{};
        const int32_t n = llama_token_to_piece(vocab_, id, buf.data(),
                                               static_cast<int32_t>(buf.size()),
                                               /*lstrip=*/0, /*special=*/false);
        if (n < 0) {
            std::vector<char> big(static_cast<size_t>(-n));
            const int32_t     n2 = llama_token_to_piece(vocab_, id, big.data(),
                                                        static_cast<int32_t>(big.size()),
                                                        0, false);
            if (n2 < 0) return {};
            return std::string(big.data(), static_cast<size_t>(n2));
        }
        return std::string(buf.data(), static_cast<size_t>(n));
    }

    bool                         verbose_     = false;
    bool                         initialized_ = false;
    llama_model*                 model_       = nullptr;
    llama_context*               ctx_         = nullptr;
    const llama_vocab*           vocab_       = nullptr;
    llama_sampler*               sampler_     = nullptr;
};

LLMEngine::LLMEngine(std::string_view model_path, bool verbose)
    : pimpl_(std::make_unique<Impl>(model_path, verbose)) {}
LLMEngine::~LLMEngine()                                = default;
LLMEngine::LLMEngine(LLMEngine&&) noexcept             = default;
LLMEngine& LLMEngine::operator=(LLMEngine&&) noexcept  = default;

std::string LLMEngine::query(std::string_view problem) {
    return pimpl_->query(problem);
}

std::string LLMEngine::query_with_error(std::string_view problem,
                                        std::string_view failed_code,
                                        std::string_view error_msg) {
    return pimpl_->query_with_error(problem, failed_code, error_msg);
}

bool LLMEngine::is_initialized() const {
    return pimpl_->is_initialized();
}
