#pragma once

#include <iomanip>
#include <sstream>
#include <string>

namespace rvx::native_validation {

inline std::string jsonEscape(const std::string& input) {
    std::ostringstream out;
    for (unsigned char c : input) {
        switch (c) {
            case '\"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(c) << std::dec;
                }
                else {
                    out << static_cast<char>(c);
                }
        }
    }
    return out.str();
}

inline std::string jsonQuote(const std::string& value) {
    return "\"" + jsonEscape(value) + "\"";
}

} // namespace rvx::native_validation
