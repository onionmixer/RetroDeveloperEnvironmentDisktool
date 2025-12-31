#ifndef RDEDISKTOOL_VERSION_H
#define RDEDISKTOOL_VERSION_H

#define RDEDISKTOOL_NAME        "rdedisktool"
#define RDEDISKTOOL_FULL_NAME   "Retro Developer Environment Disk Tool"
#define RDEDISKTOOL_VERSION     "1.0.0"
#define RDEDISKTOOL_VERSION_MAJOR 1
#define RDEDISKTOOL_VERSION_MINOR 0
#define RDEDISKTOOL_VERSION_PATCH 0

namespace rde {

constexpr const char* getVersionString() {
    return RDEDISKTOOL_VERSION;
}

constexpr int getVersionMajor() {
    return RDEDISKTOOL_VERSION_MAJOR;
}

constexpr int getVersionMinor() {
    return RDEDISKTOOL_VERSION_MINOR;
}

constexpr int getVersionPatch() {
    return RDEDISKTOOL_VERSION_PATCH;
}

} // namespace rde

#endif // RDEDISKTOOL_VERSION_H
