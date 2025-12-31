#ifndef RDEDISKTOOL_EXCEPTIONS_H
#define RDEDISKTOOL_EXCEPTIONS_H

#include <exception>
#include <string>
#include <cstdint>

namespace rde {

// Error codes
enum class DiskError {
    None = 0,
    FileNotFound,
    FileExists,
    InvalidFormat,
    UnsupportedFormat,
    ReadError,
    WriteError,
    WriteProtected,
    DiskFull,
    SectorNotFound,
    TrackNotFound,
    CRCError,
    InvalidFilename,
    FilenameTooLong,
    DirectoryFull,
    InvalidParameter,
    NotImplemented,
    InternalError
};

// Base exception class
class DiskException : public std::exception {
protected:
    DiskError m_error;
    std::string m_message;
    std::string m_fullMessage;

public:
    explicit DiskException(DiskError error, const std::string& message = "")
        : m_error(error), m_message(message) {
        m_fullMessage = errorToString(error);
        if (!message.empty()) {
            m_fullMessage += ": " + message;
        }
    }

    DiskError error() const noexcept { return m_error; }
    const std::string& message() const noexcept { return m_message; }
    const char* what() const noexcept override { return m_fullMessage.c_str(); }

    static const char* errorToString(DiskError err) {
        switch (err) {
            case DiskError::None: return "No error";
            case DiskError::FileNotFound: return "File not found";
            case DiskError::FileExists: return "File already exists";
            case DiskError::InvalidFormat: return "Invalid disk format";
            case DiskError::UnsupportedFormat: return "Unsupported format";
            case DiskError::ReadError: return "Read error";
            case DiskError::WriteError: return "Write error";
            case DiskError::WriteProtected: return "Disk is write protected";
            case DiskError::DiskFull: return "Disk is full";
            case DiskError::SectorNotFound: return "Sector not found";
            case DiskError::TrackNotFound: return "Track not found";
            case DiskError::CRCError: return "CRC error";
            case DiskError::InvalidFilename: return "Invalid filename";
            case DiskError::FilenameTooLong: return "Filename too long";
            case DiskError::DirectoryFull: return "Directory is full";
            case DiskError::InvalidParameter: return "Invalid parameter";
            case DiskError::NotImplemented: return "Not implemented";
            case DiskError::InternalError: return "Internal error";
            default: return "Unknown error";
        }
    }
};

// Specific exception classes
class FileNotFoundException : public DiskException {
public:
    explicit FileNotFoundException(const std::string& filename)
        : DiskException(DiskError::FileNotFound, filename) {}
};

class FileExistsException : public DiskException {
public:
    explicit FileExistsException(const std::string& filename)
        : DiskException(DiskError::FileExists, filename) {}
};

class InvalidFormatException : public DiskException {
public:
    explicit InvalidFormatException(const std::string& details = "")
        : DiskException(DiskError::InvalidFormat, details) {}
};

class UnsupportedFormatException : public DiskException {
public:
    explicit UnsupportedFormatException(const std::string& format)
        : DiskException(DiskError::UnsupportedFormat, format) {}
};

class ReadException : public DiskException {
public:
    explicit ReadException(const std::string& details = "")
        : DiskException(DiskError::ReadError, details) {}
};

class WriteException : public DiskException {
public:
    explicit WriteException(const std::string& details = "")
        : DiskException(DiskError::WriteError, details) {}
};

class WriteProtectedException : public DiskException {
public:
    WriteProtectedException()
        : DiskException(DiskError::WriteProtected) {}
};

class DiskFullException : public DiskException {
public:
    DiskFullException()
        : DiskException(DiskError::DiskFull) {}
};

class SectorNotFoundException : public DiskException {
public:
    SectorNotFoundException(int track, int sector)
        : DiskException(DiskError::SectorNotFound,
            "Track " + std::to_string(track) + ", Sector " + std::to_string(sector)) {}
};

class CRCException : public DiskException {
public:
    CRCException(uint16_t expected, uint16_t actual)
        : DiskException(DiskError::CRCError,
            "Expected 0x" + toHex(expected) + ", got 0x" + toHex(actual)) {}

private:
    static std::string toHex(uint16_t val) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%04X", val);
        return buf;
    }
};

class InvalidFilenameException : public DiskException {
public:
    explicit InvalidFilenameException(const std::string& filename)
        : DiskException(DiskError::InvalidFilename, filename) {}
};

class DirectoryFullException : public DiskException {
public:
    DirectoryFullException()
        : DiskException(DiskError::DirectoryFull) {}
};

class NotImplementedException : public DiskException {
public:
    explicit NotImplementedException(const std::string& feature)
        : DiskException(DiskError::NotImplemented, feature) {}
};

} // namespace rde

#endif // RDEDISKTOOL_EXCEPTIONS_H
