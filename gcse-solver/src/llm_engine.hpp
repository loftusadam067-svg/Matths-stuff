#pragma once

#include <memory>
#include <string>
#include <string_view>

class LLMEngine {
public:
    explicit LLMEngine(std::string_view model_path, bool verbose = false);
    ~LLMEngine();

    LLMEngine(const LLMEngine&)            = delete;
    LLMEngine& operator=(const LLMEngine&) = delete;
    LLMEngine(LLMEngine&&) noexcept;
    LLMEngine& operator=(LLMEngine&&) noexcept;

    [[nodiscard]] std::string query(std::string_view problem);

    [[nodiscard]] std::string query_with_error(std::string_view original_problem,
                                               std::string_view failed_code,
                                               std::string_view error_msg);

    [[nodiscard]] bool is_initialized() const;

private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};
