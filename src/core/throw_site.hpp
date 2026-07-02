#pragma once
// Throw-site capture (issue #222). err126-class crashes reach main as an
// uncaught std::system_error, but WHERE it was thrown is invisible — and the
// dll_trace "last DLL loaded" hint proved a red herring twice (ntmarta.dll is
// just the registry-ACL reader). We register a vectored exception handler on
// first-chance MSVC C++ exceptions (0xE06D7363): capture the raw stack, resolve
// each frame to module+offset, keep the most recent stacks in a small ring.
// The fatal handler prints the last one -> names the throwing module
// deterministically on the affected host, no debugger/procmon needed.
// Formatting + ring are pure and unit-tested; capture itself is Win32-only.
#include <cstdint>
#include <string>
#include <vector>

namespace icmg::core {

struct ThrowFrame {
    std::string module;      // base name, "" when unresolvable
    std::uint64_t offset;    // pc - module base (or absolute pc when unknown)
};

// "module.dll+0x1a2b" — "?" when the module is unknown.
std::string formatThrowFrame(const ThrowFrame& f);

// One line per frame, two-space indent, capped at maxFrames. "" for empty.
std::string formatThrowStack(const std::vector<ThrowFrame>& stack,
                             std::size_t maxFrames = 12);

// Push a captured stack into the ring (exposed for tests; thread-safe).
void recordThrowStack(std::vector<ThrowFrame> stack);

// Formatted most-recent throw stack, "" when none captured yet.
std::string lastThrowStackFormatted();

// Reset the ring (tests only).
void clearThrowStacksForTest();

// Install the vectored first-chance handler. Idempotent; no-op off Windows.
void installThrowSiteCapture();

}  // namespace icmg::core
