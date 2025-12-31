#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#include <cctype>

namespace rde {

/**
 * File utility functions
 */
class FileUtils {
public:
    /**
     * Read entire file into a byte vector
     */
    static std::vector<uint8_t> readFile(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            throw std::runtime_error("Cannot open file: " + path.string());
        }

        size_t size = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> data(size);
        file.read(reinterpret_cast<char*>(data.data()), size);

        return data;
    }

    /**
     * Write byte vector to file
     */
    static void writeFile(const std::filesystem::path& path,
                          const std::vector<uint8_t>& data) {
        std::ofstream file(path, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Cannot create file: " + path.string());
        }

        file.write(reinterpret_cast<const char*>(data.data()), data.size());
    }

    /**
     * Get file extension in lowercase
     */
    static std::string getExtension(const std::filesystem::path& path) {
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return ext;
    }

    /**
     * Check if path has a specific extension (case-insensitive)
     */
    static bool hasExtension(const std::filesystem::path& path,
                             const std::string& ext) {
        std::string pathExt = getExtension(path);
        std::string checkExt = ext;
        if (!checkExt.empty() && checkExt[0] != '.') {
            checkExt = "." + checkExt;
        }
        std::transform(checkExt.begin(), checkExt.end(), checkExt.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return pathExt == checkExt;
    }

    /**
     * Create parent directories if they don't exist
     */
    static void ensureParentExists(const std::filesystem::path& path) {
        auto parent = path.parent_path();
        if (!parent.empty() && !std::filesystem::exists(parent)) {
            std::filesystem::create_directories(parent);
        }
    }
};

} // namespace rde
