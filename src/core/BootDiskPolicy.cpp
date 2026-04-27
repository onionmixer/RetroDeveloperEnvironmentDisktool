#include "rdedisktool/BootDiskPolicy.h"
#include "rdedisktool/DiskImage.h"
#include "rdedisktool/FileSystemHandler.h"
#include <algorithm>
#include <cctype>
#include <set>

namespace rde {

namespace {

std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return s;
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

bool hasPathHint(const std::string& imagePath) {
    std::string p = toLower(imagePath);
    return p.find("/bootdisk/") != std::string::npos || p.find("\\bootdisk\\") != std::string::npos;
}

bool hasAny(const std::set<std::string>& names, const std::set<std::string>& probes) {
    for (const auto& p : probes) {
        if (names.find(p) != names.end()) {
            return true;
        }
    }
    return false;
}

BootDiskProfile profileFromFS(FileSystemType fsType, DiskFormat format) {
    switch (fsType) {
        case FileSystemType::DOS33: return BootDiskProfile::DOS33;
        case FileSystemType::ProDOS: return BootDiskProfile::ProDOS;
        case FileSystemType::MSXDOS1:
        case FileSystemType::MSXDOS2:
        case FileSystemType::FAT12: return BootDiskProfile::MSXDOS;
        case FileSystemType::Human68k: return BootDiskProfile::Human68k;
        case FileSystemType::HFS:
        case FileSystemType::MFS:
            return BootDiskProfile::Macintosh;
        case FileSystemType::Unknown:
        case FileSystemType::FAT16:
            break;
    }

    if (format == DiskFormat::X68000XDF || format == DiskFormat::X68000DIM) {
        return BootDiskProfile::Human68k;
    }
    if (format == DiskFormat::MSXDSK || format == DiskFormat::MSXDMK || format == DiskFormat::MSXXSA) {
        return BootDiskProfile::MSXDOS;
    }
    return BootDiskProfile::Unknown;
}

} // namespace

std::optional<BootDiskMode> BootDiskPolicy::modeFromString(const std::string& value) {
    std::string v = toLower(value);
    if (v == "strict") return BootDiskMode::Strict;
    if (v == "warn") return BootDiskMode::Warn;
    if (v == "off") return BootDiskMode::Off;
    return std::nullopt;
}

std::optional<BootDiskProfile> BootDiskPolicy::profileFromString(const std::string& value) {
    std::string v = toLower(value);
    if (v == "unknown") return BootDiskProfile::Unknown;
    if (v == "dos33") return BootDiskProfile::DOS33;
    if (v == "prodos") return BootDiskProfile::ProDOS;
    if (v == "msxdos") return BootDiskProfile::MSXDOS;
    if (v == "human68k" || v == "human") return BootDiskProfile::Human68k;
    if (v == "macintosh" || v == "mac") return BootDiskProfile::Macintosh;
    return std::nullopt;
}

const char* BootDiskPolicy::modeToString(BootDiskMode mode) {
    switch (mode) {
        case BootDiskMode::Strict: return "strict";
        case BootDiskMode::Warn: return "warn";
        case BootDiskMode::Off: return "off";
        default: return "strict";
    }
}

const char* BootDiskPolicy::profileToString(BootDiskProfile profile) {
    switch (profile) {
        case BootDiskProfile::Unknown: return "unknown";
        case BootDiskProfile::DOS33: return "dos33";
        case BootDiskProfile::ProDOS: return "prodos";
        case BootDiskProfile::MSXDOS: return "msxdos";
        case BootDiskProfile::Human68k: return "human68k";
        case BootDiskProfile::Macintosh: return "macintosh";
    }
    return "unknown";
}

const char* BootDiskPolicy::confidenceToString(Confidence confidence) {
    switch (confidence) {
        case Confidence::Low: return "low";
        case Confidence::Medium: return "medium";
        case Confidence::High: return "high";
        default: return "low";
    }
}

BootDiskDetection BootDiskPolicy::detect(const std::string& imagePath,
                                         const DiskImage& image,
                                         FileSystemHandler* handler,
                                         std::optional<BootDiskProfile> forcedProfile) {
    BootDiskDetection result;

    if (forcedProfile.has_value() && forcedProfile.value() != BootDiskProfile::Unknown) {
        result.isBootDisk = true;
        result.profile = forcedProfile.value();
        result.confidence = Confidence::High;
        result.reason = "forced profile";
        return result;
    }

    result.profile = profileFromFS(image.getFileSystemType(), image.getFormat());
    const bool pathHint = hasPathHint(imagePath);

    std::set<std::string> rootNames;
    if (handler) {
        try {
            auto files = handler->listFiles("");
            for (const auto& f : files) {
                rootNames.insert(toUpper(f.name));
            }
        } catch (...) {
            // Best effort only; leave names empty.
        }
    }

    bool hasSystemFiles = false;
    switch (result.profile) {
        case BootDiskProfile::DOS33:
            hasSystemFiles = hasAny(rootNames, {"INTBASIC", "FPBASIC", "MASTER", "BOOT13"});
            break;
        case BootDiskProfile::ProDOS:
            hasSystemFiles = hasAny(rootNames, {"PRODOS", "BASIC.SYSTEM", "QUIT.SYSTEM"});
            break;
        case BootDiskProfile::MSXDOS:
            hasSystemFiles = hasAny(rootNames, {"MSXDOS2.SYS", "COMMAND2.COM"});
            break;
        case BootDiskProfile::Human68k:
            hasSystemFiles = hasAny(rootNames, {"HUMAN.SYS", "COMMAND.X", "CONFIG.SYS", "AUTOEXEC.BAT"});
            break;
        case BootDiskProfile::Macintosh: {
            // PLAN §19.7: require LK boot block AND System+Finder presence.
            // Either root-level (e.g. typical MFS) or under "System Folder"
            // (HFS samples in MacDiskcopy/sample/). Both names must be present.
            const auto& raw = image.getRawData();
            const bool hasLK = raw.size() >= 2 && raw[0] == 'L' && raw[1] == 'K';
            if (!hasLK) {
                hasSystemFiles = false;
                break;
            }
            const bool rootHasBoth =
                rootNames.count("SYSTEM") > 0 && rootNames.count("FINDER") > 0;
            if (rootHasBoth) {
                hasSystemFiles = true;
                break;
            }
            // Check inside "System Folder" if it appears at the root.
            if (handler && rootNames.count("SYSTEM FOLDER") > 0) {
                try {
                    auto sub = handler->listFiles("System Folder");
                    bool sf = false, ff = false;
                    for (const auto& e : sub) {
                        const std::string n = toUpper(e.name);
                        if (n == "SYSTEM") sf = true;
                        if (n == "FINDER") ff = true;
                    }
                    hasSystemFiles = sf && ff;
                } catch (...) {
                    hasSystemFiles = false;
                }
            }
            break;
        }
        case BootDiskProfile::Unknown:
            break;
    }

    result.isBootDisk = hasSystemFiles || pathHint;
    if (hasSystemFiles || (pathHint && result.profile != BootDiskProfile::Unknown)) {
        result.confidence = Confidence::High;
    } else if (pathHint || result.profile != BootDiskProfile::Unknown) {
        result.confidence = Confidence::Medium;
    } else {
        result.confidence = Confidence::Low;
    }

    const bool bpbLikelyInvalid = (!handler) &&
        (result.profile == BootDiskProfile::MSXDOS || result.profile == BootDiskProfile::Human68k);

    if (hasSystemFiles) {
        result.reason = "system file pattern matched";
    } else if (bpbLikelyInvalid) {
        result.reason = "invalid_bpb_or_filesystem_init_failed";
    } else if (pathHint) {
        result.reason = "bootdisk path hint";
    } else if (result.profile != BootDiskProfile::Unknown) {
        result.reason = "filesystem profile detected";
    } else {
        result.reason = "no bootdisk evidence";
    }

    return result;
}

BootDiskDecision BootDiskPolicy::canMutate(const BootDiskDetection& det,
                                           BootDiskMode mode,
                                           MutationOp /*op*/,
                                           const std::string& /*target*/,
                                           bool forceFlag) {
    BootDiskDecision out;

    if (mode == BootDiskMode::Off) {
        return out;
    }
    if (!det.isBootDisk) {
        return out;
    }
    if (forceFlag) {
        return out;
    }

    out.allowed = false;
    out.needsForce = true;
    out.reason = std::string("Boot disk protection (") + modeToString(mode) + "): mutation is blocked "
        "(profile=" + profileToString(det.profile) + ", confidence=" + confidenceToString(det.confidence) + ")";
    return out;
}

} // namespace rde
