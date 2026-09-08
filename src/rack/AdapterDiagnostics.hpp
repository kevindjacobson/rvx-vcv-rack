#pragma once

#include <cstddef>
#include <cstdint>
#include <map>

namespace rvx {
namespace rackadapter {

// Value-only UI adapter state. PatchService supplies a complete replacement
// after each native cable scan, so warnings also clear when cables are removed.
class NativeCableDiagnostics {
public:
    bool replaceInvalidOutputs(const std::map<uint64_t, size_t>& counts) {
        if (invalidOutputs_ == counts)
            return false;
        invalidOutputs_ = counts;
        return true;
    }

    size_t invalidOutputCount(uint64_t nodeKey) const {
        std::map<uint64_t, size_t>::const_iterator it = invalidOutputs_.find(nodeKey);
        return it == invalidOutputs_.end() ? 0 : it->second;
    }

private:
    std::map<uint64_t, size_t> invalidOutputs_;
};

} // namespace rackadapter
} // namespace rvx
