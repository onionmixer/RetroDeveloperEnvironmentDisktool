#ifndef RDEDISKTOOL_COMMAND_OPTIONS_H
#define RDEDISKTOOL_COMMAND_OPTIONS_H

#include <string>
#include <vector>
#include <map>
#include <set>

namespace rdedisktool {

/**
 * Command-line option parser for consistent option handling across commands
 *
 * Usage:
 *   CommandOptions opts;
 *   opts.addFlag("force", {"-f", "--force"});
 *   opts.addValue("format", {"--format"});
 *   opts.addValue("output", {"-o", "--output"}, "default.out");
 *
 *   if (!opts.parse(args)) {
 *       // Handle error
 *   }
 *
 *   bool force = opts.hasFlag("force");
 *   std::string format = opts.getValue("format");
 *   auto positional = opts.getPositional();
 */
class CommandOptions {
public:
    /**
     * Define a boolean flag option (no value)
     * @param name Internal name for the option
     * @param aliases Command-line aliases (e.g., {"-f", "--force"})
     */
    void addFlag(const std::string& name,
                 const std::vector<std::string>& aliases);

    /**
     * Define a value option (requires an argument)
     * @param name Internal name for the option
     * @param aliases Command-line aliases
     * @param defaultValue Default value if not specified
     */
    void addValue(const std::string& name,
                  const std::vector<std::string>& aliases,
                  const std::string& defaultValue = "");

    /**
     * Parse command-line arguments
     * @param args Arguments to parse
     * @param error Optional pointer to receive error message
     * @return true if parsing succeeded
     */
    bool parse(const std::vector<std::string>& args, std::string* error = nullptr);

    /**
     * Check if a flag was set
     * @param name Option name
     * @return true if flag was present
     */
    bool hasFlag(const std::string& name) const;

    /**
     * Get a value option
     * @param name Option name
     * @return Value or empty string if not set
     */
    std::string getValue(const std::string& name) const;

    /**
     * Get a value option with default
     * @param name Option name
     * @param defaultVal Default value if not set
     * @return Value or defaultVal if not set
     */
    std::string getValue(const std::string& name, const std::string& defaultVal) const;

    /**
     * Check if a value option was explicitly set
     * @param name Option name
     * @return true if value was explicitly provided
     */
    bool hasValue(const std::string& name) const;

    /**
     * Get positional arguments (non-option arguments)
     * @return Vector of positional arguments
     */
    const std::vector<std::string>& getPositional() const;

    /**
     * Get number of positional arguments
     */
    size_t positionalCount() const { return m_positional.size(); }

    /**
     * Get a positional argument by index
     * @param index Index (0-based)
     * @return Argument or empty string if out of range
     */
    std::string getPositional(size_t index) const;

    /**
     * Clear all parsed values and reset for reuse
     */
    void reset();

private:
    struct OptionDef {
        bool isFlag;
        std::string defaultValue;
    };

    std::map<std::string, OptionDef> m_definitions;    // name -> definition
    std::map<std::string, std::string> m_aliasToName;  // alias -> name
    std::set<std::string> m_flags;                      // set flags
    std::map<std::string, std::string> m_values;        // name -> value
    std::vector<std::string> m_positional;              // positional args
};

} // namespace rdedisktool

#endif // RDEDISKTOOL_COMMAND_OPTIONS_H
