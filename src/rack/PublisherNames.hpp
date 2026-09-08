#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>

namespace rvx {
namespace rackadapter {

// ServiceRegistry synchronizes this value-only process namespace. Automatic
// names are unique across live Video I/O modules. Explicit names are preserved
// even when they collide so that publishing can report the conflict.
class PublisherNameReservations {
public:
    std::string reserve(uint64_t nodeKey, const std::string& preferred, bool automatic) {
        std::string selected = preferred;
        if (automatic) {
            std::set<std::string> occupied;
            for (std::map<uint64_t, Entry>::const_iterator it = entries_.begin();
                 it != entries_.end(); ++it) {
                if (it->first != nodeKey && !it->second.name.empty())
                    occupied.insert(it->second.name);
            }
            if (selected.empty() || occupied.count(selected)) {
                selected = "RVX";
                for (uint64_t suffix = 2; occupied.count(selected); ++suffix)
                    selected = "RVX " + std::to_string(suffix);
            }
        }
        entries_[nodeKey] = Entry(selected, automatic);
        return selected;
    }

    // A changed live setting is a user choice. Release its automatic claim and
    // retain the new value exactly, including deliberate duplicate names.
    void observe(uint64_t nodeKey, const std::string& name) {
        std::map<uint64_t, Entry>::iterator it = entries_.find(nodeKey);
        if (it == entries_.end()) {
            entries_[nodeKey] = Entry(name, false);
            return;
        }
        if (it->second.name != name) {
            it->second.name = name;
            it->second.automatic = false;
        }
    }

    void release(uint64_t nodeKey) {
        entries_.erase(nodeKey);
    }

    bool isAutomatic(uint64_t nodeKey) const {
        std::map<uint64_t, Entry>::const_iterator it = entries_.find(nodeKey);
        return it != entries_.end() && it->second.automatic;
    }

    bool hasCollision(uint64_t nodeKey) const {
        std::map<uint64_t, Entry>::const_iterator selected = entries_.find(nodeKey);
        if (selected == entries_.end() || selected->second.name.empty())
            return false;
        for (std::map<uint64_t, Entry>::const_iterator it = entries_.begin();
             it != entries_.end(); ++it) {
            if (it->first != nodeKey && it->second.name == selected->second.name)
                return true;
        }
        return false;
    }

private:
    struct Entry {
        std::string name;
        bool automatic;
        Entry() : automatic(false) {}
        Entry(std::string name, bool automatic)
            : name(name), automatic(automatic) {}
    };
    std::map<uint64_t, Entry> entries_;
};

} // namespace rackadapter
} // namespace rvx
