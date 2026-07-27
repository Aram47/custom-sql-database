#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace db {

/**
 * Computes SHA-256 digest of the given bytes.
 * @return 32-byte digest.
 */
std::vector<uint8_t> sha256(const uint8_t *data, size_t length);

/** Computes SHA-256 digest of a byte vector. */
std::vector<uint8_t> sha256(const std::vector<uint8_t> &data);

/** Computes SHA-256 digest of a string (raw bytes). */
std::vector<uint8_t> sha256(const std::string &data);

/** Encodes bytes as lowercase hexadecimal. */
std::string to_hex(const std::vector<uint8_t> &bytes);

/**
 * Decodes a hexadecimal string into bytes.
 * @return empty vector if input is invalid.
 */
std::vector<uint8_t> from_hex(const std::string &hex);

}  // namespace db
