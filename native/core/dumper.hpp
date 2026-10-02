#pragma once

#include "il2cpp_api.hpp"

#include <string>

namespace ep {

// Writes dump.cs, methods.tsv, fields.tsv, summary.txt and, last, done.txt into `dir`
// (with a trailing backslash). Call from a thread attached to `domain`.
bool RunDump(il2cpp::Domain* domain, const std::wstring& dir);

}  // namespace ep
