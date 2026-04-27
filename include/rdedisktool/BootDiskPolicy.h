#ifndef RDEDISKTOOL_BOOTDISKPOLICY_H
#define RDEDISKTOOL_BOOTDISKPOLICY_H

#include "rdedisktool/Types.h"
#include <optional>
#include <string>

namespace rde {

class DiskImage;
class FileSystemHandler;

enum class BootDiskMode {
    Strict,
    Warn,
    Off
};

enum class MutationOp {
    Add,
    Delete,
    Mkdir,
    Rmdir,
    Rename
};

enum class BootDiskProfile {
    Unknown,
    DOS33,
    ProDOS,
    MSXDOS,
    Human68k,
    Macintosh
};

enum class Confidence {
    Low,
    Medium,
    High
};

struct BootDiskDetection {
    bool isBootDisk = false;
    BootDiskProfile profile = BootDiskProfile::Unknown;
    Confidence confidence = Confidence::Low;
    std::string reason;
};

struct BootDiskDecision {
    bool allowed = true;
    bool needsForce = false;
    std::string reason;
};

class BootDiskPolicy {
public:
    static BootDiskDetection detect(const std::string& imagePath,
                                    const DiskImage& image,
                                    FileSystemHandler* handler,
                                    std::optional<BootDiskProfile> forcedProfile);

    static BootDiskDecision canMutate(const BootDiskDetection& det,
                                      BootDiskMode mode,
                                      MutationOp op,
                                      const std::string& target,
                                      bool forceFlag);

    static std::optional<BootDiskMode> modeFromString(const std::string& value);
    static std::optional<BootDiskProfile> profileFromString(const std::string& value);

    static const char* modeToString(BootDiskMode mode);
    static const char* profileToString(BootDiskProfile profile);
    static const char* confidenceToString(Confidence confidence);
};

} // namespace rde

#endif // RDEDISKTOOL_BOOTDISKPOLICY_H
