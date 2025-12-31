#include <string>
#include <algorithm>
#include <cctype>

namespace rde {

/**
 * Filename conversion utilities for different platforms
 */
class FilenameConverter {
public:
    /**
     * Convert a host filename to Apple II DOS 3.3 format
     * - Max 30 characters
     * - Uppercase only
     * - Limited character set
     */
    static std::string toAppleDOS33(const std::string& filename) {
        std::string result;
        result.reserve(30);

        for (char c : filename) {
            if (result.size() >= 30) break;

            c = std::toupper(static_cast<unsigned char>(c));

            // Valid DOS 3.3 characters: A-Z, 0-9, period, space
            if ((c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') ||
                c == '.' || c == ' ') {
                result += c;
            }
        }

        // Remove trailing spaces
        while (!result.empty() && result.back() == ' ') {
            result.pop_back();
        }

        return result;
    }

    /**
     * Convert a host filename to Apple II ProDOS format
     * - Max 15 characters
     * - Uppercase letters, digits, periods
     * - Must start with a letter
     */
    static std::string toAppleProDOS(const std::string& filename) {
        std::string result;
        result.reserve(15);

        bool started = false;
        for (char c : filename) {
            if (result.size() >= 15) break;

            c = std::toupper(static_cast<unsigned char>(c));

            if (!started) {
                // First character must be a letter
                if (c >= 'A' && c <= 'Z') {
                    result += c;
                    started = true;
                }
            } else {
                // Subsequent: A-Z, 0-9, period
                if ((c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') ||
                    c == '.') {
                    result += c;
                }
            }
        }

        return result.empty() ? "NONAME" : result;
    }

    /**
     * Convert a host filename to MSX-DOS format (8.3)
     * - 8 character name + 3 character extension
     * - Uppercase only
     */
    static std::string toMSXDOS(const std::string& filename) {
        // Find extension
        size_t dotPos = filename.rfind('.');
        std::string name, ext;

        if (dotPos != std::string::npos && dotPos > 0) {
            name = filename.substr(0, dotPos);
            ext = filename.substr(dotPos + 1);
        } else {
            name = filename;
        }

        // Convert name (max 8 chars)
        std::string resultName;
        for (char c : name) {
            if (resultName.size() >= 8) break;
            c = std::toupper(static_cast<unsigned char>(c));
            if (isValidMSXChar(c)) {
                resultName += c;
            }
        }

        // Convert extension (max 3 chars)
        std::string resultExt;
        for (char c : ext) {
            if (resultExt.size() >= 3) break;
            c = std::toupper(static_cast<unsigned char>(c));
            if (isValidMSXChar(c)) {
                resultExt += c;
            }
        }

        // Pad with spaces
        while (resultName.size() < 8) resultName += ' ';
        while (resultExt.size() < 3) resultExt += ' ';

        return resultName + resultExt;
    }

    /**
     * Convert MSX-DOS filename to host format
     */
    static std::string fromMSXDOS(const std::string& filename) {
        if (filename.size() < 11) return filename;

        std::string name = filename.substr(0, 8);
        std::string ext = filename.substr(8, 3);

        // Trim trailing spaces
        while (!name.empty() && name.back() == ' ') name.pop_back();
        while (!ext.empty() && ext.back() == ' ') ext.pop_back();

        if (ext.empty()) {
            return name;
        }
        return name + "." + ext;
    }

private:
    static bool isValidMSXChar(char c) {
        return (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') ||
               c == '_' || c == '-' || c == '!' ||
               c == '#' || c == '$' || c == '%' ||
               c == '&' || c == '@' || c == '^' ||
               c == '(' || c == ')' || c == '{' ||
               c == '}' || c == '~' || c == '`';
    }
};

} // namespace rde
