/**
 * MSX FAT Utilities
 *
 * Utility functions for FAT12/FAT16 file system operations
 * used by MSX-DOS.
 */

#include <cstdint>
#include <cstddef>
#include <vector>

namespace rde {

/**
 * FAT12/FAT16 utility functions
 */
class MSXFATUtils {
public:
    /**
     * Read a FAT12 cluster entry
     * @param fat FAT table data
     * @param cluster Cluster number
     * @return Next cluster or EOF marker
     */
    static uint16_t readFAT12Entry(const uint8_t* fat, uint16_t cluster) {
        size_t offset = cluster + (cluster / 2);
        uint16_t value;

        if (cluster & 1) {
            // Odd cluster: high 4 bits of byte, all 8 bits of next byte
            value = (fat[offset] >> 4) | (static_cast<uint16_t>(fat[offset + 1]) << 4);
        } else {
            // Even cluster: all 8 bits of byte, low 4 bits of next byte
            value = fat[offset] | ((static_cast<uint16_t>(fat[offset + 1]) & 0x0F) << 8);
        }

        return value;
    }

    /**
     * Write a FAT12 cluster entry
     * @param fat FAT table data
     * @param cluster Cluster number
     * @param value Value to write
     */
    static void writeFAT12Entry(uint8_t* fat, uint16_t cluster, uint16_t value) {
        size_t offset = cluster + (cluster / 2);

        if (cluster & 1) {
            // Odd cluster
            fat[offset] = (fat[offset] & 0x0F) | ((value & 0x0F) << 4);
            fat[offset + 1] = (value >> 4) & 0xFF;
        } else {
            // Even cluster
            fat[offset] = value & 0xFF;
            fat[offset + 1] = (fat[offset + 1] & 0xF0) | ((value >> 8) & 0x0F);
        }
    }

    /**
     * Read a FAT16 cluster entry
     * @param fat FAT table data
     * @param cluster Cluster number
     * @return Next cluster or EOF marker
     */
    static uint16_t readFAT16Entry(const uint8_t* fat, uint16_t cluster) {
        size_t offset = cluster * 2;
        return fat[offset] | (static_cast<uint16_t>(fat[offset + 1]) << 8);
    }

    /**
     * Write a FAT16 cluster entry
     * @param fat FAT table data
     * @param cluster Cluster number
     * @param value Value to write
     */
    static void writeFAT16Entry(uint8_t* fat, uint16_t cluster, uint16_t value) {
        size_t offset = cluster * 2;
        fat[offset] = value & 0xFF;
        fat[offset + 1] = (value >> 8) & 0xFF;
    }

    /**
     * Check if a FAT entry indicates end of chain (FAT12)
     */
    static bool isEOF_FAT12(uint16_t entry) {
        return entry >= 0xFF8;
    }

    /**
     * Check if a FAT entry indicates end of chain (FAT16)
     */
    static bool isEOF_FAT16(uint16_t entry) {
        return entry >= 0xFFF8;
    }

    /**
     * Check if a FAT entry indicates a bad cluster (FAT12)
     */
    static bool isBad_FAT12(uint16_t entry) {
        return entry == 0xFF7;
    }

    /**
     * Check if a FAT entry indicates a bad cluster (FAT16)
     */
    static bool isBad_FAT16(uint16_t entry) {
        return entry == 0xFFF7;
    }

    /**
     * Check if a FAT entry indicates free cluster
     */
    static bool isFree(uint16_t entry) {
        return entry == 0;
    }

    /**
     * Get cluster chain for a file
     * @param fat FAT table data
     * @param startCluster First cluster of file
     * @param isFAT16 true for FAT16, false for FAT12
     * @return Vector of cluster numbers
     */
    static std::vector<uint16_t> getClusterChain(const uint8_t* fat,
                                                  uint16_t startCluster,
                                                  bool isFAT16) {
        std::vector<uint16_t> chain;
        uint16_t cluster = startCluster;

        while (true) {
            if (cluster < 2) break;  // Invalid

            chain.push_back(cluster);

            uint16_t next = isFAT16 ? readFAT16Entry(fat, cluster)
                                    : readFAT12Entry(fat, cluster);

            if (isFAT16 ? isEOF_FAT16(next) : isEOF_FAT12(next)) break;
            if (isFAT16 ? isBad_FAT16(next) : isBad_FAT12(next)) break;
            if (isFree(next)) break;

            // Prevent infinite loops
            if (chain.size() > 65535) break;

            cluster = next;
        }

        return chain;
    }
};

} // namespace rde
