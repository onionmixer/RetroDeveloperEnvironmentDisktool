#ifndef RDEDISKTOOL_CLI_H
#define RDEDISKTOOL_CLI_H

#include "rdedisktool/Types.h"
#include <string>
#include <vector>
#include <functional>
#include <map>
#include <memory>

namespace rde {

/**
 * Command-line interface handler for rdedisktool
 */
class CLI {
public:
    // Command handler function type
    using CommandHandler = std::function<int(const std::vector<std::string>& args)>;

    CLI();
    ~CLI() = default;

    /**
     * Run the CLI with command-line arguments
     * @param argc Argument count
     * @param argv Argument values
     * @return Exit code (0 = success)
     */
    int run(int argc, char* argv[]);

    /**
     * Parse and execute a command string
     * @param args Command arguments (including command name)
     * @return Exit code
     */
    int execute(const std::vector<std::string>& args);

    /**
     * Register a command handler
     * @param command Command name
     * @param handler Handler function
     * @param description Brief description
     * @param usage Usage string
     */
    void registerCommand(const std::string& command,
                        CommandHandler handler,
                        const std::string& description,
                        const std::string& usage);

    /**
     * Print program version
     */
    void printVersion() const;

    /**
     * Print help message
     */
    void printHelp() const;

    /**
     * Print help for a specific command
     */
    void printCommandHelp(const std::string& command) const;

    /**
     * Set verbose output mode
     */
    void setVerbose(bool verbose) { m_verbose = verbose; }

    /**
     * Check if verbose mode is enabled
     */
    bool isVerbose() const { return m_verbose; }

private:
    // Command information structure
    struct CommandInfo {
        CommandHandler handler;
        std::string description;
        std::string usage;
    };

    // Registered commands
    std::map<std::string, CommandInfo> m_commands;

    // Global options
    bool m_verbose = false;

    // Built-in command handlers
    int cmdInfo(const std::vector<std::string>& args);
    int cmdList(const std::vector<std::string>& args);
    int cmdExtract(const std::vector<std::string>& args);
    int cmdAdd(const std::vector<std::string>& args);
    int cmdDelete(const std::vector<std::string>& args);
    int cmdCreate(const std::vector<std::string>& args);
    int cmdConvert(const std::vector<std::string>& args);
    int cmdDump(const std::vector<std::string>& args);
    int cmdValidate(const std::vector<std::string>& args);

    // Initialize built-in commands
    void initCommands();

    // Parse global options, return remaining args
    std::vector<std::string> parseGlobalOptions(const std::vector<std::string>& args);

    // Utility functions
    static void printError(const std::string& message);
    static void printWarning(const std::string& message);
    void printInfo(const std::string& message) const;
};

} // namespace rde

#endif // RDEDISKTOOL_CLI_H
