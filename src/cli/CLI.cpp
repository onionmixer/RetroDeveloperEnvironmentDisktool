#include "rdedisktool/CLI.h"
#include "rdedisktool/DiskImage.h"
#include "rdedisktool/DiskImageFactory.h"
#include "rdedisktool/FileSystemHandler.h"
#include "rdedisktool/filesystem/MSXDOSHandler.h"
#include "rdedisktool/filesystem/AppleProDOSHandler.h"
#include "rdedisktool/apple/AppleConstants.h"
#include "rdedisktool/msx/MSXXSAImage.h"
#include "rdedisktool/msx/MSXDiskImage.h"
#include "rdedisktool/utils/CommandOptions.h"
#include "rdedisktool/Version.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace {

std::string toLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

rde::DiskFormat formatFromString(const std::string& str) {
    std::string s = toLower(str);
    // Apple II formats
    if (s == "do" || s == "appledo") return rde::DiskFormat::AppleDO;
    if (s == "po" || s == "applepo") return rde::DiskFormat::ApplePO;
    if (s == "nib" || s == "applenib") return rde::DiskFormat::AppleNIB;
    if (s == "nb2" || s == "applenib2") return rde::DiskFormat::AppleNIB2;
    if (s == "woz" || s == "woz2" || s == "applewoz2") return rde::DiskFormat::AppleWOZ2;
    if (s == "woz1" || s == "applewoz1") return rde::DiskFormat::AppleWOZ1;
    // MSX formats
    if (s == "msxdsk" || s == "msx") return rde::DiskFormat::MSXDSK;
    if (s == "dmk" || s == "msxdmk") return rde::DiskFormat::MSXDMK;
    if (s == "xsa" || s == "msxxsa") return rde::DiskFormat::MSXXSA;
    // X68000 formats
    if (s == "xdf" || s == "x68000xdf" || s == "x68k") return rde::DiskFormat::X68000XDF;
    if (s == "dim" || s == "x68000dim") return rde::DiskFormat::X68000DIM;
    return rde::DiskFormat::Unknown;
}

rde::FileSystemType fileSystemFromString(const std::string& str) {
    std::string s = toLower(str);
    if (s == "dos33" || s == "dos3.3") return rde::FileSystemType::DOS33;
    if (s == "prodos") return rde::FileSystemType::ProDOS;
    if (s == "msxdos" || s == "msxdos1") return rde::FileSystemType::MSXDOS1;
    if (s == "msxdos2") return rde::FileSystemType::MSXDOS2;
    if (s == "fat12") return rde::FileSystemType::FAT12;
    if (s == "human68k" || s == "human") return rde::FileSystemType::Human68k;
    return rde::FileSystemType::Unknown;
}

bool isFileSystemCompatible(rde::DiskFormat format, rde::FileSystemType fsType) {
    // Apple II formats
    bool isApple = (format == rde::DiskFormat::AppleDO ||
                    format == rde::DiskFormat::ApplePO ||
                    format == rde::DiskFormat::AppleNIB ||
                    format == rde::DiskFormat::AppleNIB2 ||
                    format == rde::DiskFormat::AppleWOZ1 ||
                    format == rde::DiskFormat::AppleWOZ2);
    // MSX formats
    bool isMSX = (format == rde::DiskFormat::MSXDSK ||
                  format == rde::DiskFormat::MSXDMK);
    // X68000 formats
    bool isX68000 = (format == rde::DiskFormat::X68000XDF ||
                     format == rde::DiskFormat::X68000DIM);

    if (isApple) {
        return (fsType == rde::FileSystemType::DOS33 ||
                fsType == rde::FileSystemType::ProDOS);
    }
    if (isMSX) {
        return (fsType == rde::FileSystemType::MSXDOS1 ||
                fsType == rde::FileSystemType::MSXDOS2 ||
                fsType == rde::FileSystemType::FAT12);
    }
    if (isX68000) {
        return (fsType == rde::FileSystemType::Human68k);
    }
    return false;
}

bool parseGeometry(const std::string& spec, rde::DiskGeometry& geom) {
    std::vector<size_t> values;
    std::stringstream ss(spec);
    std::string token;
    while (std::getline(ss, token, ':')) {
        try {
            values.push_back(std::stoul(token));
        } catch (...) {
            return false;
        }
    }
    if (values.size() != 4) return false;
    geom.tracks = values[0];
    geom.sides = values[1];
    geom.sectorsPerTrack = values[2];
    geom.bytesPerSector = values[3];
    return true;
}

} // anonymous namespace

namespace rde {

CLI::CLI() {
    initCommands();
}

void CLI::initCommands() {
    registerCommand("info",
        [this](const std::vector<std::string>& args) { return cmdInfo(args); },
        "Display disk image information",
        "info <image_file>");

    registerCommand("list",
        [this](const std::vector<std::string>& args) { return cmdList(args); },
        "List files in disk image",
        "list <image_file> [path]");

    registerCommand("extract",
        [this](const std::vector<std::string>& args) { return cmdExtract(args); },
        "Extract files from disk image",
        "extract <image_file> <file> [output_path]");

    registerCommand("add",
        [this](const std::vector<std::string>& args) { return cmdAdd(args); },
        "Add file to disk image",
        "add [options] <image_file> <host_file> [target_name]\n"
        "    Options:\n"
        "      -f, --force         Overwrite existing file\n"
        "      -t, --type <type>   File type for DOS 3.3 (T/I/A/B/S/R)\n"
        "      -a, --addr <addr>   Load address for binary files (hex: 0x0803 or $0803)");

    registerCommand("delete",
        [this](const std::vector<std::string>& args) { return cmdDelete(args); },
        "Delete file from disk image",
        "delete <image_file> <file>");

    registerCommand("mkdir",
        [this](const std::vector<std::string>& args) { return cmdMkdir(args); },
        "Create directory in disk image",
        "mkdir <image_file> <directory> [-f <format>]");

    registerCommand("rmdir",
        [this](const std::vector<std::string>& args) { return cmdRmdir(args); },
        "Remove empty directory from disk image",
        "rmdir <image_file> <directory> [-f <format>]");

    registerCommand("create",
        [this](const std::vector<std::string>& args) { return cmdCreate(args); },
        "Create new disk image",
        "create <file> -f <format> [--fs <filesystem>] [-n <volume>] [-g <geometry>] [--force]");

    registerCommand("convert",
        [this](const std::vector<std::string>& args) { return cmdConvert(args); },
        "Convert disk image format",
        "convert <input_file> <output_file> [--format <format>]");

    registerCommand("dump",
        [this](const std::vector<std::string>& args) { return cmdDump(args); },
        "Dump sector/track data",
        "dump <image_file> -t <track> -s <sector> [--side <n>] [-f <format>]");

    registerCommand("validate",
        [this](const std::vector<std::string>& args) { return cmdValidate(args); },
        "Validate disk image integrity",
        "validate <image_file>");
}

void CLI::registerCommand(const std::string& command,
                          CommandHandler handler,
                          const std::string& description,
                          const std::string& usage) {
    m_commands[command] = {std::move(handler), description, usage};
}

int CLI::run(int argc, char* argv[]) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    // Parse global options
    args = parseGlobalOptions(args);

    if (args.empty()) {
        printHelp();
        return 0;
    }

    return execute(args);
}

int CLI::execute(const std::vector<std::string>& args) {
    if (args.empty()) {
        printHelp();
        return 0;
    }

    const std::string& command = args[0];

    // Check for help or version
    if (command == "help" || command == "--help" || command == "-h") {
        if (args.size() > 1) {
            printCommandHelp(args[1]);
        } else {
            printHelp();
        }
        return 0;
    }

    if (command == "version" || command == "--version" || command == "-V") {
        printVersion();
        return 0;
    }

    // Find and execute command
    auto it = m_commands.find(command);
    if (it == m_commands.end()) {
        printError("Unknown command: " + command);
        std::cerr << "Run 'rdedisktool help' for usage.\n";
        return 1;
    }

    try {
        std::vector<std::string> cmdArgs(args.begin() + 1, args.end());
        return it->second.handler(cmdArgs);
    } catch (const DiskException& e) {
        printError(e.what());
        return 1;
    } catch (const std::exception& e) {
        printError(std::string("Error: ") + e.what());
        return 1;
    }
}

std::vector<std::string> CLI::parseGlobalOptions(const std::vector<std::string>& args) {
    std::vector<std::string> remaining;

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];

        if (arg == "-v" || arg == "--verbose") {
            m_verbose = true;
        } else if (arg == "-q" || arg == "--quiet") {
            m_quiet = true;
        } else {
            remaining.push_back(arg);
        }
    }

    return remaining;
}

void CLI::printVersion() const {
    std::cout << RDEDISKTOOL_FULL_NAME << " v" << RDEDISKTOOL_VERSION << "\n";
    std::cout << "Supported platforms: Apple II, MSX, X68000\n";
}

void CLI::printHelp() const {
    printVersion();
    std::cout << "\nUsage: rdedisktool [options] <command> [arguments]\n\n";

    std::cout << "Global Options:\n";
    std::cout << "  -v, --verbose    Enable verbose output\n";
    std::cout << "  -q, --quiet      Suppress non-essential output\n";
    std::cout << "  -h, --help       Show help message\n";
    std::cout << "  -V, --version    Show version information\n";
    std::cout << "\n";

    std::cout << "Commands:\n";
    for (const auto& [name, info] : m_commands) {
        std::cout << "  " << std::left << std::setw(12) << name
                  << " " << info.description << "\n";
    }
    std::cout << "\n";

    std::cout << "Supported Formats:\n";
    std::cout << "  Apple II: .do, .dsk (DOS order), .po (ProDOS order),\n";
    std::cout << "            .nib (Nibble), .woz (WOZ v1/v2)\n";
    std::cout << "  MSX:      .dsk (Raw sector), .dmk (DMK), .xsa (XSA compressed)\n";
    std::cout << "  X68000:   .xdf (XDF), .dim (DIM)\n";
    std::cout << "\n";

    std::cout << "Run 'rdedisktool help <command>' for detailed command help.\n";
}

void CLI::printCommandHelp(const std::string& command) const {
    auto it = m_commands.find(command);
    if (it == m_commands.end()) {
        printError("Unknown command: " + command);
        return;
    }

    const auto& info = it->second;
    std::cout << "Usage: rdedisktool " << info.usage << "\n\n";
    std::cout << info.description << "\n";

    // Extended help for create command
    if (command == "create") {
        std::cout << "\nOptions:\n";
        std::cout << "  -f, --format <fmt>      Disk format (required if not detectable from extension)\n";
        std::cout << "  --fs, --filesystem <fs> Initialize with filesystem (optional)\n";
        std::cout << "  -n, --volume <name>     Volume name (optional, ignored for DOS 3.3)\n";
        std::cout << "  -g, --geometry <spec>   Custom geometry: tracks:sides:sectors:bytes\n";
        std::cout << "  --force                 Overwrite existing file\n";
        std::cout << "\nSupported Formats:\n";
        std::cout << "  Apple II: do, po, nib, nb2, woz, woz1, woz2\n";
        std::cout << "  MSX:      msxdsk, dmk\n";
        std::cout << "\nSupported Filesystems:\n";
        std::cout << "  Apple II: dos33, prodos\n";
        std::cout << "  MSX:      msxdos, fat12\n";
        std::cout << "\nDefault Geometry:\n";
        std::cout << "  Apple II: 35 tracks, 1 side, 16 sectors/track, 256 bytes/sector (140 KB)\n";
        std::cout << "  MSX:      80 tracks, 2 sides, 9 sectors/track, 512 bytes/sector (720 KB)\n";
        std::cout << "\nExamples:\n";
        std::cout << "  rdedisktool create disk.do -f do --fs dos33\n";
        std::cout << "  rdedisktool create game.po -f po --fs prodos -n MYGAME\n";
        std::cout << "  rdedisktool create msx.dsk -f msxdsk --fs msxdos -n MSXDISK\n";
        std::cout << "  rdedisktool create custom.do -f do -g 40:1:16:256\n";
        std::cout << "  rdedisktool create blank.po -f po\n";
        std::cout << "\nNote: Created disks are not bootable (no boot code included).\n";
    } else if (command == "add") {
        std::cout << "\nOptions:\n";
        std::cout << "  -f, --force        Overwrite existing file without prompting\n";
        std::cout << "\nExamples:\n";
        std::cout << "  rdedisktool add disk.po myfile.txt\n";
        std::cout << "  rdedisktool add disk.po myfile.txt TARGETNAME.TXT\n";
        std::cout << "  rdedisktool add --force disk.po myfile.txt\n";
    } else if (command == "dump") {
        std::cout << "\nOptions:\n";
        std::cout << "  -t, --track <n>    Track number (0-based, required)\n";
        std::cout << "  -s, --sector <n>   Sector number (0-based, required)\n";
        std::cout << "  --side <n>         Side number (0-based, default: 0)\n";
        std::cout << "  -f, --format <fmt> Disk format (auto-detected if not specified)\n";
        std::cout << "\nOutput Format:\n";
        std::cout << "  16 bytes per line with hex values and ASCII representation\n";
        std::cout << "\nExamples:\n";
        std::cout << "  rdedisktool dump disk.do -t 17 -s 0\n";
        std::cout << "  rdedisktool dump disk.dsk --track 0 --sector 0 --side 1\n";
        std::cout << "  rdedisktool dump disk.dsk -t 0 -s 0 -f msxdsk\n";
    } else if (command == "mkdir") {
        std::cout << "\nOptions:\n";
        std::cout << "  -f, --format <fmt> Disk format (auto-detected if not specified)\n";
        std::cout << "\nSupported File Systems:\n";
        std::cout << "  ProDOS (Apple II), MSX-DOS (MSX)\n";
        std::cout << "\nExamples:\n";
        std::cout << "  rdedisktool mkdir mydisk.dsk GAMES\n";
        std::cout << "  rdedisktool mkdir mydisk.dsk GAMES/RPG\n";
        std::cout << "\nNote: DOS 3.3 does not support subdirectories.\n";
    } else if (command == "rmdir") {
        std::cout << "\nOptions:\n";
        std::cout << "  -f, --format <fmt> Disk format (auto-detected if not specified)\n";
        std::cout << "\nExamples:\n";
        std::cout << "  rdedisktool rmdir mydisk.dsk GAMES/RPG\n";
        std::cout << "  rdedisktool rmdir mydisk.dsk GAMES\n";
        std::cout << "\nNote: Directory must be empty before removal.\n";
    } else if (command == "convert") {
        std::cout << "\nOptions:\n";
        std::cout << "  -f, --format <fmt> Output disk format (auto-detected from extension if not specified)\n";
        std::cout << "\nSupported Conversions:\n";
        std::cout << "  Apple II: do <-> po\n";
        std::cout << "  MSX:      dsk <-> dmk <-> xsa\n";
        std::cout << "\nExamples:\n";
        std::cout << "  rdedisktool convert game.do game.po\n";
        std::cout << "  rdedisktool convert game.dsk game.xsa\n";
        std::cout << "  rdedisktool convert game.xsa game.dsk -f msxdsk\n";
        std::cout << "\nNote: XSA compression achieves ~99%% ratio for typical disk images.\n";
    } else if (command == "list") {
        std::cout << "\nExamples:\n";
        std::cout << "  rdedisktool list mydisk.dsk\n";
        std::cout << "  rdedisktool list mydisk.dsk GAMES\n";
        std::cout << "  rdedisktool list mydisk.dsk GAMES/RPG\n";
    } else if (command == "extract") {
        std::cout << "\nExamples:\n";
        std::cout << "  rdedisktool extract game.dsk PLAYER.BIN ./player.bin\n";
        std::cout << "  rdedisktool extract game.dsk GAMES/GAME.COM\n";
        std::cout << "  rdedisktool extract game.dsk GAMES/RPG/SAVE.DAT ./mysave.dat\n";
    } else if (command == "delete") {
        std::cout << "\nExamples:\n";
        std::cout << "  rdedisktool delete mydisk.dsk OLDFILE.TXT\n";
        std::cout << "  rdedisktool delete mydisk.dsk GAMES/OLD.COM\n";
    } else if (command == "validate") {
        std::cout << "\nExamples:\n";
        std::cout << "  rdedisktool validate mydisk.dsk\n";
        std::cout << "  rdedisktool validate corrupted.po\n";
        std::cout << "\nValidation checks:\n";
        std::cout << "  - Disk image structure integrity\n";
        std::cout << "  - File system metadata consistency\n";
        std::cout << "  - Sector allocation bitmap verification\n";
    } else if (command == "info") {
        std::cout << "\nOptions:\n";
        std::cout << "  -v, --verbose      Show detailed information (FAT/cluster map for MSX)\n";
        std::cout << "\nExamples:\n";
        std::cout << "  rdedisktool info game.dsk\n";
        std::cout << "  rdedisktool info game.dsk -v\n";
    }
}

void CLI::printError(const std::string& message) {
    std::cerr << "Error: " << message << "\n";
}

void CLI::printWarning(const std::string& message) {
    std::cerr << "Warning: " << message << "\n";
}

void CLI::printInfo(const std::string& message) const {
    if (m_verbose) {
        std::cout << message << "\n";
    }
}

//=============================================================================
// Disk Loading Helpers
//=============================================================================

LoadedDisk CLI::loadDiskImage(const std::string& imagePath) {
    LoadedDisk result;

    // Detect format
    result.format = DiskImageFactory::detectFormat(imagePath);
    if (result.format == DiskFormat::Unknown) {
        printError("Unable to detect disk format: " + imagePath);
        return result;
    }

    // Open disk image
    try {
        result.image = DiskImageFactory::open(imagePath, result.format);
    } catch (const std::exception& e) {
        printError("Failed to open disk image: " + std::string(e.what()));
        return result;
    }

    if (!result.image) {
        printError("Failed to open disk image: " + imagePath);
        return result;
    }

    // Create filesystem handler
    result.handler = FileSystemHandler::create(result.image.get());
    if (!result.handler) {
        printError("File system not supported for this disk format");
        return result;
    }

    return result;
}

LoadedDisk CLI::loadDiskImageOnly(const std::string& imagePath) {
    LoadedDisk result;

    // Detect format
    result.format = DiskImageFactory::detectFormat(imagePath);
    if (result.format == DiskFormat::Unknown) {
        printError("Unable to detect disk format: " + imagePath);
        return result;
    }

    // Open disk image
    try {
        result.image = DiskImageFactory::open(imagePath, result.format);
    } catch (const std::exception& e) {
        printError("Failed to open disk image: " + std::string(e.what()));
        return result;
    }

    if (!result.image) {
        printError("Failed to open disk image: " + imagePath);
        return result;
    }

    // Try to create filesystem handler (optional for this method)
    result.handler = FileSystemHandler::create(result.image.get());

    return result;
}

bool CLI::saveDiskImage(DiskImage* image, const std::string& operation) {
    if (!image) {
        printError("No disk image to save");
        return false;
    }

    try {
        image->save();
        return true;
    } catch (const std::exception& e) {
        printError("Error saving disk image after " + operation + ": " + std::string(e.what()));
        return false;
    }
}

//=============================================================================
// Command Implementations
//=============================================================================

int CLI::cmdInfo(const std::vector<std::string>& args) {
    if (args.empty()) {
        printError("Missing image file argument");
        printCommandHelp("info");
        return 1;
    }

    const std::string& imagePath = args[0];

    try {
        // Detect format
        DiskFormat format = DiskImageFactory::detectFormat(imagePath);
        Platform platform = DiskImageFactory::getPlatformForFormat(format);
        DiskGeometry geom = DiskImageFactory::getDefaultGeometry(format);

        std::cout << "File: " << imagePath << "\n";
        std::cout << "Format: " << formatToString(format) << "\n";
        std::cout << "Platform: " << platformToString(platform) << "\n";

        // Try to open and get more details
        if (DiskImageFactory::isFormatSupported(format)) {
            auto image = DiskImageFactory::open(imagePath, format);
            geom = image->getGeometry();

            std::cout << "\nGeometry:\n";
            std::cout << "  Tracks: " << geom.tracks << "\n";
            std::cout << "  Sides: " << geom.sides << "\n";
            std::cout << "  Sectors/Track: " << geom.sectorsPerTrack << "\n";
            std::cout << "  Bytes/Sector: " << geom.bytesPerSector << "\n";
            std::cout << "  Total Size: " << geom.totalSize() << " bytes\n";

            FileSystemType fsType = image->getFileSystemType();
            std::cout << "\nFile System: " << fileSystemTypeToString(fsType) << "\n";

            // Show additional filesystem info if detected
            if (fsType != FileSystemType::Unknown) {
                auto handler = FileSystemHandler::create(image.get());
                if (handler) {
                    std::string volumeName = handler->getVolumeName();
                    if (!volumeName.empty()) {
                        std::cout << "Volume: " << volumeName << "\n";
                    }
                    std::cout << "Free Space: " << handler->getFreeSpace() << " bytes\n";
                    std::cout << "Total Space: " << handler->getTotalSpace() << " bytes\n";

                    // Show FAT/cluster information for MSX-DOS in verbose mode
                    if (m_verbose && (fsType == FileSystemType::MSXDOS1 || fsType == FileSystemType::MSXDOS2)) {
                        auto* msxHandler = dynamic_cast<MSXDOSHandler*>(handler.get());
                        if (msxHandler) {
                            auto clusterInfo = msxHandler->getClusterInfo();

                            std::cout << "\nCluster Information:\n";
                            std::cout << "  Total Clusters:    " << clusterInfo.totalClusters << "\n";
                            std::cout << "  Used Clusters:     " << clusterInfo.usedClusters << "\n";
                            std::cout << "  Free Clusters:     " << clusterInfo.freeClusters << "\n";
                            if (clusterInfo.badClusters > 0) {
                                std::cout << "  Bad Clusters:      " << clusterInfo.badClusters << "\n";
                            }
                            std::cout << "  Cluster Size:      " << (msxHandler->getSectorsPerCluster() * msxHandler->getBytesPerSector()) << " bytes\n";

                            std::cout << "\nFAT Cluster Map:\n";
                            std::cout << "  Cluster 0: 0x" << std::hex << std::uppercase << std::setfill('0')
                                      << std::setw(3) << clusterInfo.clusterMap[0] << " (Media descriptor)\n";
                            std::cout << "  Cluster 1: 0x" << std::setw(3) << clusterInfo.clusterMap[1] << " (Reserved)\n";
                            std::cout << std::dec;

                            // Show first 32 data clusters (2-33)
                            size_t showCount = std::min(static_cast<size_t>(32), static_cast<size_t>(clusterInfo.totalClusters));
                            for (size_t i = 0; i < showCount; ++i) {
                                uint16_t cluster = static_cast<uint16_t>(i + 2);
                                uint16_t val = clusterInfo.clusterMap[cluster];

                                std::cout << "  Cluster " << std::setw(3) << std::setfill(' ') << cluster << ": ";
                                if (val == 0x000) {
                                    std::cout << "FREE\n";
                                } else if (val >= 0xFF8) {
                                    std::cout << "EOF (0x" << std::hex << std::uppercase << std::setfill('0')
                                              << std::setw(3) << val << std::dec << ")\n";
                                } else if (val == 0xFF7) {
                                    std::cout << "BAD\n";
                                } else {
                                    std::cout << "-> " << val << "\n";
                                }
                            }

                            if (clusterInfo.totalClusters > 32) {
                                std::cout << "  ... (" << (clusterInfo.totalClusters - 32) << " more clusters)\n";
                            }
                        }
                    }
                }
            }

            std::cout << "Write Protected: "
                      << (image->isWriteProtected() ? "Yes" : "No") << "\n";

            if (m_verbose) {
                std::cout << "\nDiagnostics:\n" << image->getDiagnostics() << "\n";
            }
        } else {
            std::cout << "\nNote: Full format support not yet implemented.\n";
            std::cout << "Default geometry for this format:\n";
            std::cout << "  Tracks: " << geom.tracks << "\n";
            std::cout << "  Sides: " << geom.sides << "\n";
            std::cout << "  Sectors/Track: " << geom.sectorsPerTrack << "\n";
            std::cout << "  Bytes/Sector: " << geom.bytesPerSector << "\n";
        }

        return 0;
    } catch (const DiskException& e) {
        printError(e.what());
        return 1;
    }
}

int CLI::cmdList(const std::vector<std::string>& args) {
    if (args.empty()) {
        printError("Missing image file argument");
        printCommandHelp("list");
        return 1;
    }

    const std::string& imagePath = args[0];
    std::string path = args.size() > 1 ? args[1] : "";

    try {
        auto disk = loadDiskImage(imagePath);
        if (!disk) {
            return 1;
        }

        auto files = disk.handler->listFiles(path);

        if (m_quiet) {
            // Quiet mode: output only filenames
            for (const auto& file : files) {
                std::cout << file.name << "\n";
            }
        } else {
            std::cout << "Directory listing for: " << imagePath << "\n";
            std::cout << "Volume: " << disk.handler->getVolumeName() << "\n\n";

            std::cout << std::left << std::setw(30) << "Name"
                      << std::right << std::setw(10) << "Size"
                      << std::setw(6) << "Type"
                      << std::setw(6) << "Attr" << "\n";
            std::cout << std::string(52, '-') << "\n";

            size_t totalSize = 0;
            for (const auto& file : files) {
                std::cout << std::left << std::setw(30) << file.name
                          << std::right << std::setw(10) << file.size;

                // File type indicator
                std::string typeStr = file.isDirectory ? "DIR" : "FILE";
                std::cout << std::setw(6) << typeStr;

                // Attributes
                std::string attrStr;
                if (file.attributes & 0x01) attrStr += "R";  // Read-only
                if (file.attributes & 0x02) attrStr += "H";  // Hidden
                if (file.attributes & 0x04) attrStr += "S";  // System
                if (file.attributes & 0x80) attrStr += "L";  // Locked
                std::cout << std::setw(6) << attrStr << "\n";

                totalSize += file.size;
            }

            std::cout << std::string(52, '-') << "\n";
            std::cout << files.size() << " file(s), " << totalSize << " bytes\n";
            std::cout << "Free space: " << disk.handler->getFreeSpace() << " bytes\n";
        }

        return 0;
    } catch (const DiskException& e) {
        printError(e.what());
        return 1;
    }
}

int CLI::cmdExtract(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        printError("Missing arguments");
        printCommandHelp("extract");
        return 1;
    }

    const std::string& imagePath = args[0];
    const std::string& filename = args[1];

    // Determine output path
    std::string outputPath;
    if (args.size() > 2) {
        outputPath = args[2];
    } else {
        // Extract just the filename from the path (remove directory components)
        size_t lastSlash = filename.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            outputPath = filename.substr(lastSlash + 1);
        } else {
            outputPath = filename;
        }
    }

    try {
        auto disk = loadDiskImage(imagePath);
        if (!disk) {
            return 1;
        }

        if (!disk.handler->fileExists(filename)) {
            printError("File not found: " + filename);
            return 1;
        }

        auto data = disk.handler->readFile(filename);

        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile) {
            printError("Unable to create output file: " + outputPath);
            return 1;
        }

        outFile.write(reinterpret_cast<const char*>(data.data()), data.size());
        outFile.close();

        if (!m_quiet) {
            std::cout << "Extracted: " << filename << " -> " << outputPath
                      << " (" << data.size() << " bytes)\n";
        }

        return 0;
    } catch (const DiskException& e) {
        printError(e.what());
        return 1;
    }
}

// Helper function to parse DOS 3.3 file type from string
static uint8_t parseFileTypeString(const std::string& typeStr) {
    if (typeStr.empty()) return 0;

    char c = std::toupper(static_cast<unsigned char>(typeStr[0]));
    switch (c) {
        case 'T': return AppleConstants::DOS33::FILETYPE_TEXT;
        case 'I': return AppleConstants::DOS33::FILETYPE_INTEGER;
        case 'A': return AppleConstants::DOS33::FILETYPE_APPLESOFT;
        case 'B': return AppleConstants::DOS33::FILETYPE_BINARY;
        case 'S': return AppleConstants::DOS33::FILETYPE_STYPE;
        case 'R': return AppleConstants::DOS33::FILETYPE_RELOCATABLE;
        default:
            throw std::invalid_argument("Invalid file type: " + typeStr +
                " (use T, I, A, B, S, or R)");
    }
}

// Helper function to parse address string (supports 0x, $, or decimal)
static uint16_t parseAddressString(const std::string& addrStr) {
    if (addrStr.empty()) return 0;

    std::string numStr = addrStr;
    int base = 10;

    // Handle hex prefixes: 0x or $
    if (addrStr.size() > 2 && (addrStr.substr(0, 2) == "0x" || addrStr.substr(0, 2) == "0X")) {
        base = 16;
        numStr = addrStr.substr(2);
    } else if (addrStr[0] == '$') {
        base = 16;
        numStr = addrStr.substr(1);
    }

    try {
        size_t pos = 0;
        unsigned long value = std::stoul(numStr, &pos, base);
        if (pos != numStr.size()) {
            throw std::invalid_argument("Invalid address format");
        }
        if (value > 0xFFFF) {
            throw std::out_of_range("Address exceeds 16-bit range (0x0000-0xFFFF)");
        }
        return static_cast<uint16_t>(value);
    } catch (const std::exception&) {
        throw std::invalid_argument("Invalid address: " + addrStr +
            " (use decimal, 0x1234, or $1234)");
    }
}

int CLI::cmdAdd(const std::vector<std::string>& args) {
    // Parse options using CommandOptions
    rdedisktool::CommandOptions opts;
    opts.addFlag("force", {"-f", "--force"});
    opts.addValue("type", {"-t", "--type"});
    opts.addValue("addr", {"-a", "--addr"});

    std::string parseError;
    if (!opts.parse(args, &parseError)) {
        printError(parseError);
        printCommandHelp("add");
        return 1;
    }

    if (opts.positionalCount() < 2) {
        printError("Missing arguments");
        printCommandHelp("add");
        return 1;
    }

    bool forceOverwrite = opts.hasFlag("force");
    const std::string& imagePath = opts.getPositional(0);
    const std::string& hostFile = opts.getPositional(1);
    std::string targetName = opts.getPositional(2);

    // Parse file type and load address options
    uint8_t fileType = 0;
    uint16_t loadAddress = 0;

    try {
        std::string typeStr = opts.getValue("type");
        if (!typeStr.empty()) {
            fileType = parseFileTypeString(typeStr);
        }

        std::string addrStr = opts.getValue("addr");
        if (!addrStr.empty()) {
            loadAddress = parseAddressString(addrStr);
        }
    } catch (const std::exception& e) {
        printError(e.what());
        return 1;
    }

    // Extract filename from path if target name not specified
    if (targetName.empty()) {
        size_t pos = hostFile.find_last_of("/\\");
        targetName = (pos != std::string::npos) ? hostFile.substr(pos + 1) : hostFile;
    }

    try {
        // Read host file
        std::ifstream inFile(hostFile, std::ios::binary | std::ios::ate);
        if (!inFile) {
            printError("Unable to open host file: " + hostFile);
            return 1;
        }

        size_t fileSize = inFile.tellg();
        inFile.seekg(0);

        std::vector<uint8_t> data(fileSize);
        inFile.read(reinterpret_cast<char*>(data.data()), fileSize);
        inFile.close();

        // Open disk image
        auto disk = loadDiskImage(imagePath);
        if (!disk) {
            return 1;
        }

        // Check if file already exists
        if (disk.handler->fileExists(targetName)) {
            if (!forceOverwrite) {
                printError("File already exists: " + targetName + "\nUse --force to overwrite");
                return 1;
            }
            // Delete existing file before adding new one
            if (!disk.handler->deleteFile(targetName)) {
                printError("Failed to delete existing file: " + targetName);
                return 1;
            }
            if (!m_quiet) {
                printWarning("Overwriting existing file: " + targetName);
            }
        }

        // Check free space
        if (disk.handler->getFreeSpace() < data.size()) {
            printError("Not enough space on disk");
            return 1;
        }

        // Write file
        FileMetadata metadata;
        metadata.targetName = targetName;
        metadata.fileType = fileType;
        metadata.loadAddress = loadAddress;

        if (!disk.handler->writeFile(targetName, data, metadata)) {
            printError("Failed to write file to disk image");
            return 1;
        }

        // Save disk image
        if (!saveDiskImage(disk.image.get(), "add")) {
            return 1;
        }

        if (!m_quiet) {
            std::cout << "Added: " << hostFile << " -> " << targetName
                      << " (" << data.size() << " bytes)";
            if (fileType != 0 || loadAddress != 0) {
                std::cout << " [";
                if (fileType != 0) {
                    const char* typeNames[] = {"T", "I", "A", "", "B", "", "", "", "S"};
                    if (fileType <= 8) {
                        std::cout << "type=" << typeNames[fileType];
                    } else if (fileType == 0x10) {
                        std::cout << "type=R";
                    }
                }
                if (fileType != 0 && loadAddress != 0) {
                    std::cout << ", ";
                }
                if (loadAddress != 0) {
                    std::cout << "addr=$" << std::hex << std::uppercase
                              << std::setw(4) << std::setfill('0') << loadAddress
                              << std::dec;
                }
                std::cout << "]";
            }
            std::cout << "\n";
        }

        return 0;
    } catch (const DiskException& e) {
        printError(e.what());
        return 1;
    }
}

int CLI::cmdDelete(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        printError("Missing arguments");
        printCommandHelp("delete");
        return 1;
    }

    const std::string& imagePath = args[0];
    const std::string& filename = args[1];

    try {
        auto disk = loadDiskImage(imagePath);
        if (!disk) {
            return 1;
        }

        if (!disk.handler->fileExists(filename)) {
            printError("File not found: " + filename);
            return 1;
        }

        if (!disk.handler->deleteFile(filename)) {
            printError("Failed to delete file: " + filename);
            return 1;
        }

        // Save disk image
        if (!saveDiskImage(disk.image.get(), "delete")) {
            return 1;
        }

        if (!m_quiet) {
            std::cout << "Deleted: " << filename << "\n";
        }

        return 0;
    } catch (const DiskException& e) {
        printError(e.what());
        return 1;
    }
}

int CLI::cmdMkdir(const std::vector<std::string>& args) {
    // Parse options using CommandOptions
    rdedisktool::CommandOptions opts;
    // No options defined - mkdir just takes positional arguments

    std::string parseError;
    if (!opts.parse(args, &parseError)) {
        printError(parseError);
        printCommandHelp("mkdir");
        return 1;
    }

    if (opts.positionalCount() < 2) {
        printError("Missing arguments");
        printCommandHelp("mkdir");
        return 1;
    }

    const std::string& imagePath = opts.getPositional(0);
    const std::string& dirPath = opts.getPositional(1);

    try {
        auto disk = loadDiskImage(imagePath);
        if (!disk) {
            return 1;
        }

        // Use unified directory interface
        if (!disk.handler->supportsDirectories()) {
            printError("This file system does not support directories");
            return 1;
        }

        if (!disk.handler->createDirectory(dirPath)) {
            printError("Failed to create directory: " + dirPath);
            return 1;
        }

        if (!saveDiskImage(disk.image.get(), "mkdir")) {
            return 1;
        }

        if (!m_quiet) {
            std::cout << "Created directory: " << dirPath << "\n";
        }

        return 0;
    } catch (const DiskException& e) {
        printError(e.what());
        return 1;
    }
}

int CLI::cmdRmdir(const std::vector<std::string>& args) {
    // Parse options using CommandOptions
    rdedisktool::CommandOptions opts;
    // No options defined - rmdir just takes positional arguments

    std::string parseError;
    if (!opts.parse(args, &parseError)) {
        printError(parseError);
        printCommandHelp("rmdir");
        return 1;
    }

    if (opts.positionalCount() < 2) {
        printError("Missing arguments");
        printCommandHelp("rmdir");
        return 1;
    }

    const std::string& imagePath = opts.getPositional(0);
    const std::string& dirPath = opts.getPositional(1);

    try {
        auto disk = loadDiskImage(imagePath);
        if (!disk) {
            return 1;
        }

        // Use unified directory interface
        if (!disk.handler->supportsDirectories()) {
            printError("This file system does not support directories");
            return 1;
        }

        if (!disk.handler->deleteDirectory(dirPath)) {
            printError("Failed to remove directory: " + dirPath + " (directory may not be empty)");
            return 1;
        }

        if (!saveDiskImage(disk.image.get(), "rmdir")) {
            return 1;
        }

        if (!m_quiet) {
            std::cout << "Removed directory: " << dirPath << "\n";
        }

        return 0;
    } catch (const DiskException& e) {
        printError(e.what());
        return 1;
    }
}

int CLI::cmdCreate(const std::vector<std::string>& args) {
    // Parse options using CommandOptions
    rdedisktool::CommandOptions opts;
    opts.addValue("format", {"-f", "--format"});
    opts.addValue("filesystem", {"--fs", "--filesystem"});
    opts.addValue("volume", {"-n", "--volume"});
    opts.addValue("geometry", {"-g", "--geometry"});
    opts.addFlag("force", {"--force"});

    std::string parseError;
    if (!opts.parse(args, &parseError)) {
        printError(parseError);
        printCommandHelp("create");
        return 1;
    }

    if (opts.positionalCount() < 1) {
        printError("Missing output file argument");
        printCommandHelp("create");
        return 1;
    }

    const std::string& outputPath = opts.getPositional(0);
    std::string formatStr = opts.getValue("format");
    std::string filesystemStr = opts.getValue("filesystem");
    std::string volumeName = opts.getValue("volume");
    std::string geometryStr = opts.getValue("geometry");
    bool force = opts.hasFlag("force");

    // Determine disk format
    DiskFormat format = DiskFormat::Unknown;
    if (!formatStr.empty()) {
        format = formatFromString(formatStr);
        if (format == DiskFormat::Unknown) {
            printError("Unknown disk format: " + formatStr);
            printError("Supported formats: do, po, nib, nb2, woz, woz1, woz2, msxdsk, dmk, xdf, dim");
            return 1;
        }
    } else {
        // Try to infer from extension
        std::filesystem::path p(outputPath);
        std::string ext = p.extension().string();
        if (!ext.empty()) {
            format = DiskImageFactory::getFormatFromExtension(ext);
        }
        if (format == DiskFormat::Unknown) {
            printError("Cannot determine format from extension. Use --format option.");
            printError("Supported formats: do, po, nib, nb2, woz, woz1, woz2, msxdsk, dmk, xdf, dim");
            return 1;
        }
    }

    // XSA is read-only
    if (format == DiskFormat::MSXXSA) {
        printError("XSA format is read-only and cannot be created");
        return 1;
    }

    // Check if file already exists
    if (std::filesystem::exists(outputPath) && !force) {
        printError("File already exists: " + outputPath);
        printError("Use --force to overwrite");
        return 1;
    }

    // Determine geometry
    DiskGeometry geometry;
    if (!geometryStr.empty()) {
        if (!parseGeometry(geometryStr, geometry)) {
            printError("Invalid geometry format. Expected: tracks:sides:sectors:bytes");
            printError("Example: 35:1:16:256 (Apple II) or 80:2:9:512 (MSX 720KB)");
            return 1;
        }
    } else {
        geometry = DiskImageFactory::getDefaultGeometry(format);
    }

    // Validate and determine filesystem type
    FileSystemType fsType = FileSystemType::Unknown;
    if (!filesystemStr.empty()) {
        fsType = fileSystemFromString(filesystemStr);
        if (fsType == FileSystemType::Unknown) {
            printError("Unknown filesystem: " + filesystemStr);
            printError("Supported filesystems: dos33, prodos, msxdos, fat12");
            return 1;
        }
        if (!isFileSystemCompatible(format, fsType)) {
            printError("Filesystem '" + filesystemStr + "' is not compatible with format '" +
                       formatToString(format) + "'");
            printError("Apple II formats support: dos33, prodos");
            printError("MSX formats support: msxdos, fat12");
            return 1;
        }
    }

    try {
        // Create the disk image
        auto image = DiskImageFactory::create(format, geometry);
        if (!image) {
            printError("Failed to create disk image");
            return 1;
        }

        // Initialize filesystem if requested
        if (fsType != FileSystemType::Unknown) {
            auto handler = FileSystemHandler::createForType(fsType);
            if (!handler) {
                printError("Failed to create filesystem handler for: " + filesystemStr);
                return 1;
            }

            // Connect disk to handler (without parsing) and format
            handler->setDisk(image.get());

            if (!handler->format(volumeName)) {
                printError("Failed to format disk with filesystem");
                return 1;
            }
        }

        // Save the disk image
        image->save(outputPath);

        // Print success message
        if (!m_quiet) {
            std::cout << "Created: " << outputPath << "\n";
            std::cout << "Format: " << formatToString(format) << "\n";
            std::cout << "Size: " << geometry.totalSize() << " bytes";
            if (geometry.totalSize() >= 1024) {
                std::cout << " (" << (geometry.totalSize() / 1024) << " KB)";
            }
            std::cout << "\n";
            std::cout << "Geometry: " << geometry.tracks << " tracks, "
                      << geometry.sides << " side(s), "
                      << geometry.sectorsPerTrack << " sectors/track, "
                      << geometry.bytesPerSector << " bytes/sector\n";

            if (fsType != FileSystemType::Unknown) {
                std::cout << "Filesystem: " << fileSystemTypeToString(fsType) << "\n";
                if (!volumeName.empty()) {
                    std::cout << "Volume: " << volumeName << "\n";
                }
            }
        }

        return 0;

    } catch (const DiskException& e) {
        printError(e.what());
        return 1;
    } catch (const std::exception& e) {
        printError(std::string("Error: ") + e.what());
        return 1;
    }
}

int CLI::cmdConvert(const std::vector<std::string>& args) {
    // Parse options using CommandOptions
    rdedisktool::CommandOptions opts;
    opts.addValue("format", {"-f", "--format"});

    std::string parseError;
    if (!opts.parse(args, &parseError)) {
        printError(parseError);
        printCommandHelp("convert");
        return 1;
    }

    if (opts.positionalCount() < 2) {
        printError("Missing input or output file");
        printCommandHelp("convert");
        return 1;
    }

    const std::string& inputPath = opts.getPositional(0);
    const std::string& outputPath = opts.getPositional(1);
    std::string formatStr = opts.getValue("format");

    try {

        // Open input image
        auto inputImage = DiskImageFactory::open(inputPath);
        if (!inputImage) {
            printError("Failed to open input file: " + inputPath);
            return 1;
        }

        // Determine output format
        DiskFormat outputFormat = DiskFormat::Unknown;
        if (!formatStr.empty()) {
            outputFormat = stringToFormat(formatStr);
        }

        if (outputFormat == DiskFormat::Unknown) {
            // Try to determine from output file extension
            outputFormat = DiskImageFactory::getFormatFromExtension(
                std::filesystem::path(outputPath).extension().string());
        }

        if (outputFormat == DiskFormat::Unknown) {
            printError("Cannot determine output format. Use --format option.");
            printError("Supported formats: do, po, dsk, dmk, msxdsk, xsa, xdf, dim");
            return 1;
        }

        // Check platform compatibility
        Platform inputPlatform = DiskImageFactory::getPlatformForFormat(inputImage->getFormat());
        Platform outputPlatform = DiskImageFactory::getPlatformForFormat(outputFormat);

        if (inputPlatform != outputPlatform) {
            printError("Cross-platform conversion is not supported.");
            printError(std::string("Input: ") + formatToString(inputImage->getFormat()) +
                      ", Output: " + formatToString(outputFormat));
            return 1;
        }

        // Get geometry from input
        DiskGeometry geom = inputImage->getGeometry();

        std::unique_ptr<DiskImage> outputImage;
        size_t sectorsConverted = 0;

        // Special handling for XSA output format
        if (outputFormat == DiskFormat::MSXXSA) {
            // For XSA, we need to get the raw data from the input MSX disk
            auto* msxInput = dynamic_cast<MSXDiskImage*>(inputImage.get());
            if (!msxInput) {
                printError("Input must be an MSX disk image for XSA conversion");
                return 1;
            }

            // Read all raw data from the input image
            std::vector<uint8_t> rawData;
            rawData.reserve(geom.tracks * geom.sides * geom.sectorsPerTrack * geom.bytesPerSector);

            for (size_t track = 0; track < geom.tracks; ++track) {
                for (size_t side = 0; side < geom.sides; ++side) {
                    for (size_t sector = 0; sector < geom.sectorsPerTrack; ++sector) {
                        try {
                            auto data = inputImage->readSector(track, side, sector);
                            rawData.insert(rawData.end(), data.begin(), data.end());
                            ++sectorsConverted;
                        } catch (const std::exception& e) {
                            // Fill with zeros for missing sectors
                            rawData.insert(rawData.end(), geom.bytesPerSector, 0);
                            if (m_verbose) {
                                printWarning("Failed to read sector T" + std::to_string(track) +
                                           "/S" + std::to_string(side) + "/H" + std::to_string(sector) +
                                           ": " + e.what());
                            }
                        }
                    }
                }
            }

            // Create XSA from raw data
            std::string origFilename = std::filesystem::path(inputPath).stem().string() + ".dsk";
            outputImage = MSXXSAImage::createFromRawData(rawData, origFilename);
        } else {
            // Standard conversion: create output image and copy sectors
            outputImage = DiskImageFactory::create(outputFormat, geom);
            if (!outputImage) {
                printError("Failed to create output file: " + outputPath);
                return 1;
            }

            // Copy all sectors
            for (size_t track = 0; track < geom.tracks; ++track) {
                for (size_t side = 0; side < geom.sides; ++side) {
                    for (size_t sector = 0; sector < geom.sectorsPerTrack; ++sector) {
                        try {
                            auto data = inputImage->readSector(track, side, sector);
                            outputImage->writeSector(track, side, sector, data);
                            ++sectorsConverted;
                        } catch (const std::exception& e) {
                            if (m_verbose) {
                                printWarning("Failed to copy sector T" + std::to_string(track) +
                                           "/S" + std::to_string(side) + "/H" + std::to_string(sector) +
                                           ": " + e.what());
                            }
                        }
                    }
                }
            }
        }

        // Save output image
        outputImage->save(outputPath);

        if (!m_quiet) {
            std::cout << "Converted: " << inputPath << " -> " << outputPath << "\n";
            std::cout << "Format: " << formatToString(inputImage->getFormat())
                      << " -> " << formatToString(outputFormat) << "\n";
            std::cout << "Sectors: " << sectorsConverted << " copied\n";
        }

        return 0;

    } catch (const DiskException& e) {
        printError(e.what());
        return 1;
    } catch (const std::exception& e) {
        printError(std::string("Error: ") + e.what());
        return 1;
    }
}

int CLI::cmdDump(const std::vector<std::string>& args) {
    // Parse options using CommandOptions
    rdedisktool::CommandOptions opts;
    opts.addValue("track", {"-t", "--track"});
    opts.addValue("sector", {"-s", "--sector"});
    opts.addValue("side", {"--side"}, "0");
    opts.addValue("format", {"-f", "--format"});

    std::string parseError;
    if (!opts.parse(args, &parseError)) {
        printError(parseError);
        printCommandHelp("dump");
        return 1;
    }

    if (opts.positionalCount() < 1) {
        printError("Missing image file argument");
        printCommandHelp("dump");
        return 1;
    }

    const std::string& imagePath = opts.getPositional(0);
    std::string formatStr = opts.getValue("format");

    // Parse and validate track
    std::string trackStr = opts.getValue("track");
    if (trackStr.empty()) {
        printError("Missing --track argument");
        printCommandHelp("dump");
        return 1;
    }
    int track;
    try {
        track = std::stoi(trackStr);
    } catch (...) {
        printError("Invalid track number: " + trackStr);
        return 1;
    }

    // Parse and validate sector
    std::string sectorStr = opts.getValue("sector");
    if (sectorStr.empty()) {
        printError("Missing --sector argument");
        printCommandHelp("dump");
        return 1;
    }
    int sector;
    try {
        sector = std::stoi(sectorStr);
    } catch (...) {
        printError("Invalid sector number: " + sectorStr);
        return 1;
    }

    // Parse side (has default value of 0)
    int side;
    try {
        side = std::stoi(opts.getValue("side", "0"));
    } catch (...) {
        printError("Invalid side number: " + opts.getValue("side"));
        return 1;
    }

    try {
        // Determine format
        DiskFormat format = DiskFormat::Unknown;
        if (!formatStr.empty()) {
            format = formatFromString(formatStr);
            if (format == DiskFormat::Unknown) {
                printError("Unknown disk format: " + formatStr);
                return 1;
            }
        } else {
            format = DiskImageFactory::detectFormat(imagePath);
            if (format == DiskFormat::Unknown) {
                printError("Unable to detect disk format. Use --format option.");
                return 1;
            }
        }

        // Open disk image
        auto image = DiskImageFactory::open(imagePath, format);
        auto geom = image->getGeometry();

        // Validate parameters
        if (static_cast<size_t>(track) >= geom.tracks) {
            printError("Track " + std::to_string(track) + " out of range (0-" +
                      std::to_string(geom.tracks - 1) + ")");
            return 1;
        }
        if (static_cast<size_t>(side) >= geom.sides) {
            printError("Side " + std::to_string(side) + " out of range (0-" +
                      std::to_string(geom.sides - 1) + ")");
            return 1;
        }
        if (static_cast<size_t>(sector) >= geom.sectorsPerTrack) {
            printError("Sector " + std::to_string(sector) + " out of range (0-" +
                      std::to_string(geom.sectorsPerTrack - 1) + ")");
            return 1;
        }

        // Read sector
        auto data = image->readSector(track, side, sector);

        // Print header
        std::cout << "Sector dump: Track " << track
                  << ", Side " << side
                  << ", Sector " << sector
                  << " (" << data.size() << " bytes)\n";
        std::cout << "Offset  00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F  0123456789ABCDEF\n";
        std::cout << "------  -----------------------  -----------------------  ----------------\n";

        // Print hex dump (16 bytes per line)
        const size_t BYTES_PER_LINE = 16;
        for (size_t offset = 0; offset < data.size(); offset += BYTES_PER_LINE) {
            // Offset
            std::cout << std::hex << std::uppercase << std::setfill('0')
                      << std::setw(6) << offset << "  ";

            // Hex bytes
            for (size_t j = 0; j < BYTES_PER_LINE; ++j) {
                if (offset + j < data.size()) {
                    std::cout << std::setw(2) << static_cast<int>(data[offset + j]);
                } else {
                    std::cout << "  ";
                }
                std::cout << " ";
                if (j == 7) std::cout << " ";  // Extra space at midpoint
            }

            // ASCII representation
            std::cout << " ";
            for (size_t j = 0; j < BYTES_PER_LINE && offset + j < data.size(); ++j) {
                uint8_t c = data[offset + j];
                std::cout << (std::isprint(c) ? static_cast<char>(c) : '.');
            }
            std::cout << "\n";
        }

        std::cout << std::dec;  // Reset to decimal
        return 0;

    } catch (const DiskException& e) {
        printError(e.what());
        return 1;
    }
}

int CLI::cmdValidate(const std::vector<std::string>& args) {
    if (args.empty()) {
        printError("Missing image file argument");
        printCommandHelp("validate");
        return 1;
    }

    const std::string& imagePath = args[0];

    try {
        DiskFormat format = DiskImageFactory::detectFormat(imagePath);

        if (format == DiskFormat::Unknown) {
            printError("Unable to detect disk format");
            return 1;
        }

        if (!m_quiet) {
            std::cout << "File: " << imagePath << "\n";
            std::cout << "Format: " << formatToString(format) << "\n";
        }

        if (!DiskImageFactory::isFormatSupported(format)) {
            printWarning("Full validation not available for this format");
            std::cout << "Status: Format detected but handler not implemented\n";
            return 0;
        }

        auto image = DiskImageFactory::open(imagePath, format);

        // Basic disk image validation
        bool basicValid = image->validate();

        if (!m_quiet) {
            std::cout << "Disk image: " << (basicValid ? "Valid" : "Invalid") << "\n";
        }

        // Extended filesystem validation
        auto handler = FileSystemHandler::create(image.get());
        if (handler) {
            if (!m_quiet) {
                std::cout << "File system: " << fileSystemTypeToString(handler->getType()) << "\n\n";
            }

            ValidationResult result = handler->validateExtended();

            if (!m_quiet) {
                // Print issues
                for (const auto& issue : result.issues) {
                    std::string prefix;
                    switch (issue.severity) {
                        case ValidationSeverity::Error:
                            prefix = "ERROR: ";
                            break;
                        case ValidationSeverity::Warning:
                            prefix = "WARNING: ";
                            break;
                        case ValidationSeverity::Info:
                            prefix = "INFO: ";
                            break;
                    }

                    std::cout << prefix << issue.message;
                    if (!issue.location.empty()) {
                        std::cout << " [" << issue.location << "]";
                    }
                    std::cout << "\n";
                }

                std::cout << "\nSummary: ";
                std::cout << result.errorCount << " error(s), ";
                std::cout << result.warningCount << " warning(s)\n";
                std::cout << "Status: " << (result.isValid ? "Valid" : "Invalid") << "\n";
            }

            return result.isValid ? 0 : 1;
        } else {
            if (!m_quiet) {
                std::cout << "File system: Not detected\n";
                std::cout << "Status: " << (basicValid ? "Valid" : "Invalid") << "\n";
            }
            return basicValid ? 0 : 1;
        }
    } catch (const DiskException& e) {
        printError(e.what());
        return 1;
    }
}

} // namespace rde
