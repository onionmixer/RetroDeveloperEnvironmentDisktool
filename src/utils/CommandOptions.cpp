#include "rdedisktool/utils/CommandOptions.h"

namespace rdedisktool {

void CommandOptions::addFlag(const std::string& name,
                              const std::vector<std::string>& aliases) {
    OptionDef def;
    def.isFlag = true;
    def.defaultValue = "";
    m_definitions[name] = def;

    for (const auto& alias : aliases) {
        m_aliasToName[alias] = name;
    }
}

void CommandOptions::addValue(const std::string& name,
                               const std::vector<std::string>& aliases,
                               const std::string& defaultValue) {
    OptionDef def;
    def.isFlag = false;
    def.defaultValue = defaultValue;
    m_definitions[name] = def;

    for (const auto& alias : aliases) {
        m_aliasToName[alias] = name;
    }

    // Set default value if provided
    if (!defaultValue.empty()) {
        m_values[name] = defaultValue;
    }
}

bool CommandOptions::parse(const std::vector<std::string>& args, std::string* error) {
    // Clear previous parse results but keep definitions
    m_flags.clear();
    m_positional.clear();

    // Reset values to defaults
    m_values.clear();
    for (const auto& [name, def] : m_definitions) {
        if (!def.isFlag && !def.defaultValue.empty()) {
            m_values[name] = def.defaultValue;
        }
    }

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];

        // Check if this is an option (starts with -)
        if (!arg.empty() && arg[0] == '-') {
            // Check for -- which means end of options
            if (arg == "--") {
                // All remaining args are positional
                for (size_t j = i + 1; j < args.size(); ++j) {
                    m_positional.push_back(args[j]);
                }
                break;
            }

            // Look up the alias
            auto it = m_aliasToName.find(arg);
            if (it == m_aliasToName.end()) {
                if (error) {
                    *error = "Unknown option: " + arg;
                }
                return false;
            }

            const std::string& name = it->second;
            const OptionDef& def = m_definitions.at(name);

            if (def.isFlag) {
                m_flags.insert(name);
            } else {
                // Value option - need next argument
                if (i + 1 >= args.size()) {
                    if (error) {
                        *error = "Option " + arg + " requires a value";
                    }
                    return false;
                }
                ++i;
                m_values[name] = args[i];
            }
        } else {
            // Positional argument
            m_positional.push_back(arg);
        }
    }

    return true;
}

bool CommandOptions::hasFlag(const std::string& name) const {
    return m_flags.find(name) != m_flags.end();
}

std::string CommandOptions::getValue(const std::string& name) const {
    auto it = m_values.find(name);
    if (it != m_values.end()) {
        return it->second;
    }
    return "";
}

std::string CommandOptions::getValue(const std::string& name, const std::string& defaultVal) const {
    auto it = m_values.find(name);
    if (it != m_values.end()) {
        return it->second;
    }
    return defaultVal;
}

bool CommandOptions::hasValue(const std::string& name) const {
    auto it = m_values.find(name);
    if (it == m_values.end()) {
        return false;
    }

    // Check if it was explicitly set (not just default)
    auto defIt = m_definitions.find(name);
    if (defIt != m_definitions.end() && !defIt->second.isFlag) {
        // If the value is different from default, it was explicitly set
        if (it->second != defIt->second.defaultValue) {
            return true;
        }
    }

    return !it->second.empty();
}

const std::vector<std::string>& CommandOptions::getPositional() const {
    return m_positional;
}

std::string CommandOptions::getPositional(size_t index) const {
    if (index < m_positional.size()) {
        return m_positional[index];
    }
    return "";
}

void CommandOptions::reset() {
    m_flags.clear();
    m_values.clear();
    m_positional.clear();

    // Reset values to defaults
    for (const auto& [name, def] : m_definitions) {
        if (!def.isFlag && !def.defaultValue.empty()) {
            m_values[name] = def.defaultValue;
        }
    }
}

} // namespace rdedisktool
