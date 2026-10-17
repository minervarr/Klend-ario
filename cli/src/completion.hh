#pragma once

#include <string>
#include <string_view>

// Shell completion scripts, as pure text.
//
// The scripts are generated rather than read from an install directory: the
// CLI is a single binary and `completion bash` must work from a fresh checkout
// with no setup. All three shells share one per-verb option table inside
// completion.cc; if a verb or flag is added, update that table and `args.cc`.
//
// Unknown shells return an empty string; the command layer turns that into a
// usage error.

namespace klend_ario::cli {

std::string completionScript(std::string_view shell);

} // namespace klend_ario::cli
