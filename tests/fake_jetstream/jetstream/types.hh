#pragma once

#include <any>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace Jetstream {

using U32 = std::uint32_t;
using F32 = float;
enum class Result { SUCCESS, ERROR };

namespace Parser {
struct Value {
    std::any data;

    Value() = default;
    Value(const char* value) : data(std::string(value)) {}
    Value(std::string value) : data(std::move(value)) {}
    template<std::size_t Size>
    Value(const char (&value)[Size]) : data(std::string(value)) {}
    template<typename T>
    Value(const T& value) : data(value) {}
};

using Map = std::map<std::string, Value>;
using Sequence = std::vector<Map>;
}  // namespace Parser

using TensorMap = std::map<std::string, int>;

}  // namespace Jetstream

#define JST_BLOCK_TYPE(...) static constexpr bool fakeBlockType = true
#define JST_BLOCK_DOMAIN(...) static constexpr bool fakeBlockDomain = true
#define JST_BLOCK_PARAMS(...) static constexpr bool fakeBlockParams = true
#define JST_BLOCK_DESCRIPTION(...) static_assert(true)
#define JST_MODULE_TYPE(...) static constexpr bool fakeModuleType = true
#define JST_MODULE_PARAMS(...) static constexpr bool fakeModuleParams = true
#define JST_CHECK(expression) do { \
    const auto jst_result = (expression); \
    if (jst_result != ::Jetstream::Result::SUCCESS) return jst_result; \
} while (false)
#define JST_REGISTER_BLOCK(...)
