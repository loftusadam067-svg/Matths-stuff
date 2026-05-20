#pragma once

#include <memory>
#include <string>
#include <string_view>

struct ExecutorResult {
    bool        success;
    std::string value;
    std::string error;
};

class ExecutorEngine {
public:
    ExecutorEngine();
    ~ExecutorEngine();

    ExecutorEngine(const ExecutorEngine&)            = delete;
    ExecutorEngine& operator=(const ExecutorEngine&) = delete;
    ExecutorEngine(ExecutorEngine&&) noexcept;
    ExecutorEngine& operator=(ExecutorEngine&&) noexcept;

    [[nodiscard]] ExecutorResult execute(std::string_view code);

private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};
