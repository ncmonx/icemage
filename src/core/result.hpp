// v1.40.1 C++23 std::expected adoption.
// `Result<T>` = expected<T, Error>. Pilot rolls out per release; legacy
// int-return paths coexist until v1.40.4 bulk conversion.
//
// Use:
//   icmg::core::Result<int> doThing();
//   auto r = doThing();
//   if (!r) { log(r.error().msg); return 1; }
//   return r.value();
#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace icmg::core {

struct Error {
    int         code{1};      // exit-style code (0 = ok, but ok = expected has value)
    std::string msg;          // human-readable

    Error() = default;
    Error(int c, std::string m) : code(c), msg(std::move(m)) {}
    explicit Error(std::string m) : msg(std::move(m)) {}
};

template <class T>
using Result = std::expected<T, Error>;

inline std::unexpected<Error> err(int code, std::string msg) {
    return std::unexpected<Error>(Error{code, std::move(msg)});
}
inline std::unexpected<Error> err(std::string msg) {
    return std::unexpected<Error>(Error{1, std::move(msg)});
}

}  // namespace icmg::core
