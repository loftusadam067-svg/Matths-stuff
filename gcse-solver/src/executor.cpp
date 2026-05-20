#include "executor.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <exception>
#include <sstream>
#include <string>
#include <string_view>

#include <exprtk.hpp>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace {

constexpr int    kLuaTimeoutSeconds  = 2;
constexpr size_t kSignificantFigures = 10;
constexpr int    kHookInterval       = 10'000;

[[nodiscard]] std::string format_number(double v) {
    if (std::isnan(v)) {
        return "NaN";
    }
    if (std::isinf(v)) {
        return v < 0 ? "-Infinity" : "Infinity";
    }

    std::ostringstream ss;
    ss.precision(static_cast<std::streamsize>(kSignificantFigures));
    ss << v;
    std::string out = ss.str();

    // Ensure floats always show a decimal point ("5" -> "5.0") for non-scientific output.
    if (out.find('.') == std::string::npos && out.find('e') == std::string::npos) {
        out += ".0";
    }
    return out;
}

[[nodiscard]] bool needs_lua(std::string_view code) {
    if (code.find('\n') != std::string_view::npos) {
        return true;
    }
    static const std::array<std::string_view, 11> kKeywords{
        "return ", "local ", "if ", "for ", "while ", "do ",
        "then ",   "end ",   "--",  "::",   "function "};
    for (auto kw : kKeywords) {
        if (code.find(kw) != std::string_view::npos) {
            return true;
        }
    }
    if (code.find("..") != std::string_view::npos) {
        return true;
    }
    if (code.find("math.") != std::string_view::npos) {
        return true;
    }
    return false;
}

struct TimedHookContext {
    std::chrono::steady_clock::time_point start{};
    bool                                  timed_out{false};

    [[nodiscard]] bool deadline_passed() const {
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start);
        return elapsed.count() >= kLuaTimeoutSeconds;
    }
};

void lua_timeout_hook(lua_State* L, lua_Debug* /*ar*/) {
    lua_getfield(L, LUA_REGISTRYINDEX, "gcse_timeout_ctx");
    auto* ctx = static_cast<TimedHookContext*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (ctx && ctx->deadline_passed()) {
        ctx->timed_out = true;
        luaL_error(L, "timeout: execution exceeded %d seconds", kLuaTimeoutSeconds);
    }
}

void sandbox_lua(lua_State* L) {
    static constexpr std::array<const char*, 6> kBanned{
        "io", "os", "package", "require", "dofile", "loadfile"};
    for (const char* name : kBanned) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }
}

}  // namespace

class ExecutorEngine::Impl {
public:
    [[nodiscard]] ExecutorResult execute(std::string_view code) {
        std::string trimmed = trim(code);
        if (trimmed.empty()) {
            return {false, "", "empty code"};
        }
        if (needs_lua(trimmed)) {
            return execute_lua(trimmed);
        }
        return execute_exprtk(trimmed);
    }

private:
    [[nodiscard]] static std::string trim(std::string_view s) {
        const auto not_space = [](unsigned char c) { return !std::isspace(c); };
        auto       b         = std::find_if(s.begin(), s.end(), not_space);
        auto       e         = std::find_if(s.rbegin(), s.rend(), not_space).base();
        return (b < e) ? std::string(b, e) : std::string{};
    }

    [[nodiscard]] static ExecutorResult execute_exprtk(const std::string& code) {
        using symbol_table_t = exprtk::symbol_table<double>;
        using expression_t   = exprtk::expression<double>;
        using parser_t       = exprtk::parser<double>;

        symbol_table_t symtab;
        symtab.add_constants();  // pi, epsilon, inf
        expression_t expr;
        expr.register_symbol_table(symtab);

        parser_t parser;
        if (!parser.compile(code, expr)) {
            return {false, "", std::string{"exprtk parse error: "} + parser.error()};
        }

        try {
            const double value = expr.value();
            if (std::isnan(value)) {
                return {false, "", "result is NaN (undefined operation)"};
            }
            if (std::isinf(value)) {
                return {false, "", "result is Infinity (division by zero?)"};
            }
            return {true, format_number(value), ""};
        } catch (const std::exception& e) {
            return {false, "", std::string{"exprtk runtime error: "} + e.what()};
        } catch (...) {
            return {false, "", "exprtk: unknown runtime error"};
        }
    }

    [[nodiscard]] static ExecutorResult execute_lua(const std::string& code) {
        lua_State* L = luaL_newstate();
        if (!L) {
            return {false, "", "lua: failed to create state"};
        }

        luaL_openlibs(L);
        sandbox_lua(L);

        TimedHookContext ctx{std::chrono::steady_clock::now(), false};
        lua_pushlightuserdata(L, &ctx);
        lua_setfield(L, LUA_REGISTRYINDEX, "gcse_timeout_ctx");
        lua_sethook(L, lua_timeout_hook, LUA_MASKCOUNT, kHookInterval);

        const int top_before = lua_gettop(L);
        if (luaL_loadstring(L, code.c_str()) != LUA_OK) {
            std::string err = "lua load error: ";
            err += lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown";
            lua_close(L);
            return {false, "", err};
        }

        if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK) {
            std::string err = ctx.timed_out
                                  ? "lua timeout: exceeded " + std::to_string(kLuaTimeoutSeconds) + "s"
                                  : std::string{"lua runtime error: "}
                                        + (lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown");
            lua_close(L);
            return {false, "", err};
        }

        const int returned = lua_gettop(L) - top_before;
        if (returned == 0) {
            lua_close(L);
            return {false, "", "lua script did not return a value"};
        }

        ExecutorResult result;
        const int      idx = -returned;

        if (lua_type(L, idx) == LUA_TNUMBER) {
            const double v = lua_tonumber(L, idx);
            if (std::isnan(v)) {
                result = {false, "", "lua result is NaN"};
            } else if (std::isinf(v)) {
                result = {false, "", "lua result is Infinity"};
            } else {
                result = {true, format_number(v), ""};
            }
        } else if (lua_type(L, idx) == LUA_TSTRING) {
            std::string s = lua_tostring(L, idx);
            if (s.rfind("ERROR", 0) == 0) {
                result = {false, "", s};
            } else if (s == "NOT_MATH") {
                result = {false, "", "non-mathematical input"};
            } else {
                result = {true, s, ""};
            }
        } else if (lua_type(L, idx) == LUA_TBOOLEAN) {
            result = {true, lua_toboolean(L, idx) ? "true" : "false", ""};
        } else {
            result = {false, "", "lua returned unsupported type"};
        }

        lua_close(L);
        return result;
    }
};

ExecutorEngine::ExecutorEngine() : pimpl_(std::make_unique<Impl>()) {}
ExecutorEngine::~ExecutorEngine()                                    = default;
ExecutorEngine::ExecutorEngine(ExecutorEngine&&) noexcept            = default;
ExecutorEngine& ExecutorEngine::operator=(ExecutorEngine&&) noexcept = default;

ExecutorResult ExecutorEngine::execute(std::string_view code) {
    return pimpl_->execute(code);
}
