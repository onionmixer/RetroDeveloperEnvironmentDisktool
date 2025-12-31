#include <ctime>
#include <cstdint>

namespace rde {

/**
 * Timestamp conversion utilities for different platforms
 */
class TimestampUtils {
public:
    /**
     * ProDOS timestamp format:
     * - Date: bits 15-9 = year (0-127 from 1900), bits 8-5 = month, bits 4-0 = day
     * - Time: bits 15-8 = hour, bits 7-0 = minute
     */
    struct ProDOSTimestamp {
        uint16_t date;
        uint16_t time;
    };

    /**
     * MSX-DOS timestamp format (same as FAT):
     * - Date: bits 15-9 = year (from 1980), bits 8-5 = month, bits 4-0 = day
     * - Time: bits 15-11 = hour, bits 10-5 = minute, bits 4-0 = second/2
     */
    struct MSXDOSTimestamp {
        uint16_t date;
        uint16_t time;
    };

    /**
     * Convert std::time_t to ProDOS timestamp
     */
    static ProDOSTimestamp toProDOS(std::time_t time) {
        ProDOSTimestamp result = {0, 0};

        if (time <= 0) return result;

        std::tm* tm = std::localtime(&time);
        if (!tm) return result;

        // ProDOS year is 0-127 from 1900
        int year = tm->tm_year;  // Years since 1900
        if (year < 0) year = 0;
        if (year > 127) year = 127;

        result.date = static_cast<uint16_t>(
            (year << 9) |
            ((tm->tm_mon + 1) << 5) |
            tm->tm_mday
        );

        result.time = static_cast<uint16_t>(
            (tm->tm_hour << 8) |
            tm->tm_min
        );

        return result;
    }

    /**
     * Convert ProDOS timestamp to std::time_t
     */
    static std::time_t fromProDOS(const ProDOSTimestamp& ts) {
        std::tm tm = {};

        tm.tm_year = (ts.date >> 9) & 0x7F;
        tm.tm_mon = ((ts.date >> 5) & 0x0F) - 1;
        tm.tm_mday = ts.date & 0x1F;
        tm.tm_hour = (ts.time >> 8) & 0xFF;
        tm.tm_min = ts.time & 0xFF;
        tm.tm_sec = 0;
        tm.tm_isdst = -1;

        return std::mktime(&tm);
    }

    /**
     * Convert std::time_t to MSX-DOS timestamp
     */
    static MSXDOSTimestamp toMSXDOS(std::time_t time) {
        MSXDOSTimestamp result = {0, 0};

        if (time <= 0) return result;

        std::tm* tm = std::localtime(&time);
        if (!tm) return result;

        // MSX-DOS year is from 1980
        int year = tm->tm_year - 80;  // Years since 1980
        if (year < 0) year = 0;
        if (year > 127) year = 127;

        result.date = static_cast<uint16_t>(
            (year << 9) |
            ((tm->tm_mon + 1) << 5) |
            tm->tm_mday
        );

        result.time = static_cast<uint16_t>(
            (tm->tm_hour << 11) |
            (tm->tm_min << 5) |
            (tm->tm_sec / 2)
        );

        return result;
    }

    /**
     * Convert MSX-DOS timestamp to std::time_t
     */
    static std::time_t fromMSXDOS(const MSXDOSTimestamp& ts) {
        std::tm tm = {};

        tm.tm_year = ((ts.date >> 9) & 0x7F) + 80;  // From 1980
        tm.tm_mon = ((ts.date >> 5) & 0x0F) - 1;
        tm.tm_mday = ts.date & 0x1F;
        tm.tm_hour = (ts.time >> 11) & 0x1F;
        tm.tm_min = (ts.time >> 5) & 0x3F;
        tm.tm_sec = (ts.time & 0x1F) * 2;
        tm.tm_isdst = -1;

        return std::mktime(&tm);
    }

    /**
     * Get current time as ProDOS timestamp
     */
    static ProDOSTimestamp currentProDOS() {
        return toProDOS(std::time(nullptr));
    }

    /**
     * Get current time as MSX-DOS timestamp
     */
    static MSXDOSTimestamp currentMSXDOS() {
        return toMSXDOS(std::time(nullptr));
    }
};

} // namespace rde
