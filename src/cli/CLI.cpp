#include "rdedisktool/CLI.h"
#include "rdedisktool/DiskImage.h"
#include "rdedisktool/DiskImageFactory.h"
#include "rdedisktool/FileSystemHandler.h"
#include "rdedisktool/Version.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <fstream>

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
        "add <image_file> <host_file> [target_name] [options]");

    registerCommand("delete",
        [this](const std::vector<std::string>& args) { return cmdDelete(args); },
        "Delete file from disk image",
        "delete <image_file> <file>");

    registerCommand("create",
        [this](const std::vector<std::string>& args) { return cmdCreate(args); },
        "Create new disk image",
        "create <image_file> --format <format> [--size <size>]");

    registerCommand("convert",
        [this](const std::vector<std::string>& args) { return cmdConvert(args); },
        "Convert disk image format",
        "convert <input_file> <output_file> [--format <format>]");

    registerCommand("dump",
        [this](const std::vector<std::string>& args) { return cmdDump(args); },
        "Dump sector/track data",
        "dump <image_file> --track <n> [--sector <n>] [--side <n>]");

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
            m_verbose = false;
        } else {
            remaining.push_back(arg);
        }
    }

    return remaining;
}

void CLI::printVersion() const {
    std::cout << RDEDISKTOOL_FULL_NAME << " v" << RDEDISKTOOL_VERSION << "\n";
    std::cout << "Supported platforms: Apple II, MSX\n";
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

            std::cout << "\nFile System: "
                      << (image->getFileSystemType() != FileSystemType::Unknown
                          ? "Detected" : "Unknown") << "\n";

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
        DiskFormat format = DiskImageFactory::detectFormat(imagePath);
        if (format == DiskFormat::Unknown) {
            printError("Unable to detect disk format");
            return 1;
        }

        auto image = DiskImageFactory::open(imagePath, format);
        auto handler = FileSystemHandler::create(image.get());

        if (!handler) {
            printError("File system not supported for this disk format");
            return 1;
        }

        auto files = handler->listFiles(path);

        std::cout << "Directory listing for: " << imagePath << "\n";
        std::cout << "Volume: " << handler->getVolumeName() << "\n\n";

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
        std::cout << "Free space: " << handler->getFreeSpace() << " bytes\n";

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
    std::string outputPath = args.size() > 2 ? args[2] : filename;

    try {
        DiskFormat format = DiskImageFactory::detectFormat(imagePath);
        if (format == DiskFormat::Unknown) {
            printError("Unable to detect disk format");
            return 1;
        }

        auto image = DiskImageFactory::open(imagePath, format);
        auto handler = FileSystemHandler::create(image.get());

        if (!handler) {
            printError("File system not supported for this disk format");
            return 1;
        }

        if (!handler->fileExists(filename)) {
            printError("File not found: " + filename);
            return 1;
        }

        auto data = handler->readFile(filename);

        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile) {
            printError("Unable to create output file: " + outputPath);
            return 1;
        }

        outFile.write(reinterpret_cast<const char*>(data.data()), data.size());
        outFile.close();

        std::cout << "Extracted: " << filename << " -> " << outputPath
                  << " (" << data.size() << " bytes)\n";

        return 0;
    } catch (const DiskException& e) {
        printError(e.what());
        return 1;
    }
}

int CLI::cmdAdd(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        printError("Missing arguments");
        printCommandHelp("add");
        return 1;
    }

    const std::string& imagePath = args[0];
    const std::string& hostFile = args[1];
    std::string targetName = args.size() > 2 ? args[2] : "";

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
        DiskFormat format = DiskImageFactory::detectFormat(imagePath);
        if (format == DiskFormat::Unknown) {
            printError("Unable to detect disk format");
            return 1;
        }

        auto image = DiskImageFactory::open(imagePath, format);
        auto handler = FileSystemHandler::create(image.get());

        if (!handler) {
            printError("File system not supported for this disk format");
            return 1;
        }

        // Check free space
        if (handler->getFreeSpace() < data.size()) {
            printError("Not enough space on disk");
            return 1;
        }

        // Write file
        FileMetadata metadata;
        metadata.targetName = targetName;

        if (!handler->writeFile(targetName, data, metadata)) {
            printError("Failed to write file to disk image");
            return 1;
        }

        // Save disk image
        image->save();

        std::cout << "Added: " << hostFile << " -> " << targetName
                  << " (" << data.size() << " bytes)\n";

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
        DiskFormat format = DiskImageFactory::detectFormat(imagePath);
        if (format == DiskFormat::Unknown) {
            printError("Unable to detect disk format");
            return 1;
        }

        auto image = DiskImageFactory::open(imagePath, format);
        auto handler = FileSystemHandler::create(image.get());

        if (!handler) {
            printError("File system not supported for this disk format");
            return 1;
        }

        if (!handler->fileExists(filename)) {
            printError("File not found: " + filename);
            return 1;
        }

        if (!handler->deleteFile(filename)) {
            printError("Failed to delete file: " + filename);
            return 1;
        }

        // Save disk image
        image->save();

        std::cout << "Deleted: " << filename << "\n";

        return 0;
    } catch (const DiskException& e) {
        printError(e.what());
        return 1;
    }
}

int CLI::cmdCreate(const std::vector<std::string>& args) {
    if (args.empty()) {
        printError("Missing output file argument");
        printCommandHelp("create");
        return 1;
    }

    printError("Disk creation not yet implemented");
    return 1;
}

int CLI::cmdConvert(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        printError("Missing arguments");
        printCommandHelp("convert");
        return 1;
    }

    printError("Format conversion not yet implemented");
    return 1;
}

int CLI::cmdDump(const std::vector<std::string>& args) {
    if (args.empty()) {
        printError("Missing image file argument");
        printCommandHelp("dump");
        return 1;
    }

    printError("Sector dump not yet implemented");
    return 1;
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

        std::cout << "File: " << imagePath << "\n";
        std::cout << "Format: " << formatToString(format) << "\n";

        if (!DiskImageFactory::isFormatSupported(format)) {
            printWarning("Full validation not available for this format");
            std::cout << "Status: Format detected but handler not implemented\n";
            return 0;
        }

        auto image = DiskImageFactory::open(imagePath, format);

        if (image->validate()) {
            std::cout << "Status: Valid\n";
            return 0;
        } else {
            std::cout << "Status: Invalid or corrupted\n";
            return 1;
        }
    } catch (const DiskException& e) {
        printError(e.what());
        return 1;
    }
}

} // namespace rde
