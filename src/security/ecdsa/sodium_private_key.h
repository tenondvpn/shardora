#pragma once

#include <sodium.h>
#include <string>
#include <vector>
#include <mutex>

#include "common/encode.h"
#include "common/log.h"
#include "security/security.h"

/**
 * Macro definitions for hardcoded Whitebox Keys.
 * In a production environment, REPLACE_WHITEBOX_SK should be 
 * generated/obfuscated via external tooling.
 */
#ifndef REPLACE_WHITEBOX_PK
#error "The compilation parameter REPLACE_WHITEBOX_PK must be set." 
#endif

#ifndef REPLACE_WHITEBOX_SK
#error "The compilation parameter REPLACE_WHITEBOX_SK must be set." 
#endif

namespace shardora {
namespace security {

class KeyManager {
public:
    /**
     * @brief Returns the thread-safe singleton instance.
     */
    static KeyManager& Instance() {
        static KeyManager instance;
        return instance;
    }

    // Delete copy constructor and assignment operator for Singleton integrity
    KeyManager(const KeyManager&) = delete;
    KeyManager& operator=(const KeyManager&) = delete;

    /**
     * @brief Encrypts a raw key using the Whitebox Public Key.
     * @param raw_key The plaintext key string.
     * @return Encrypted hex string, or empty string on failure.
     */
    static std::string SealKey(const std::string& raw_key) {
        if (sodium_init() < 0) return "";

        std::vector<unsigned char> ciphertext(crypto_box_SEALBYTES + raw_key.size());
        
        if (crypto_box_seal(ciphertext.data(), 
                            reinterpret_cast<const unsigned char*>(raw_key.data()), 
                            raw_key.size(), 
                            kWhiteboxPublicKey) != 0) {
            return "";
        }

        // Convert to hex for file storage
        return common::Encode::HexEncode(std::string((char*)ciphertext.data(), ciphertext.size()));
    }

    /**
     * @brief Initializes the manager by decrypting the sealed private key.
     * @param sealed_prikey The encrypted key (Sealed Box format).
     * @return 0 on success, -1 on failure.
     */
    int Initialize(const std::string& sealed_prikey) {
        std::lock_guard<std::mutex> lock(mutex_);
        // Clean up any existing sensitive data before re-initialization
        CleanupInternal();

        // Ensure libsodium is initialized
        if (sodium_init() < 0) return -1;

        if (sealed_prikey.size() < crypto_box_SEALBYTES) {
            SHARDORA_DEBUG("Ciphertext size: %zu is too short to be valid", sealed_prikey.size());
            return kSecurityError; // Ciphertext too short
        }

        size_t decrypted_len = sealed_prikey.size() - crypto_box_SEALBYTES;

        /**
         * Use sodium_malloc to allocate memory. 
         * It provides guard pages and prevents the memory from being swapped to disk.
         */
        protected_key_ = (unsigned char*)sodium_malloc(decrypted_len);
        if (!protected_key_) {
            SHARDORA_DEBUG("Failed to allocate protected memory for decrypted key");
            return kSecurityError;
        }
        /**
         * Decrypt the sealed box using the hardcoded Whitebox Keypair.
         * The Whitebox SK is essentially the 'Master Key' that unlocks the User Key.
         */
        if (crypto_box_seal_open(
                protected_key_, 
                reinterpret_cast<const unsigned char*>(sealed_prikey.data()), 
                sealed_prikey.size(),
                kWhiteboxPublicKey, 
                kWhiteboxPrivateKey) != 0) {
            CleanupInternal();
            SHARDORA_DEBUG("Failed to decrypt sealed key with Whitebox Keypair");
            return kSecurityError;
        }

        protected_key_length_ = decrypted_len;

        // Set the memory region to Read-Only to prevent accidental tampering
        sodium_mprotect_readonly(protected_key_);
        
        return kSecuritySuccess;
    }

    /**
     * @brief Accessor for the decrypted key.
     * @return Pointer to protected memory.
     */
    const unsigned char* GetProtectedKey() const { return protected_key_; }
    size_t GetKeyLength() const { return protected_key_length_; }

    /**
     * @brief Securely wipes and frees the key memory on destruction.
     */
    ~KeyManager() {
        CleanupInternal();
    }

private:
    KeyManager() : protected_key_(nullptr), protected_key_length_(0) {}

    /**
     * @brief Internal helper to securely zero-out and free libsodium memory.
     */
    void CleanupInternal() {
        if (protected_key_) {
            // sodium_free automatically calls sodium_memzero before freeing
            sodium_free(protected_key_);
            protected_key_ = nullptr;
            protected_key_length_ = 0;
        }
    }

    // Static Whitebox Keypair constants
    static constexpr unsigned char kWhiteboxPublicKey[crypto_box_PUBLICKEYBYTES] = { REPLACE_WHITEBOX_PK };
    static constexpr unsigned char kWhiteboxPrivateKey[crypto_box_SECRETKEYBYTES] = { REPLACE_WHITEBOX_SK };

    unsigned char* protected_key_;
    size_t protected_key_length_;
    std::mutex mutex_;
};

} // namespace security
} // namespace shardora
