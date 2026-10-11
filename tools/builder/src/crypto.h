#ifndef TB_CRYPTO_H
#define TB_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

void sha256(const void* data, size_t len, uint8_t out[32]);
char* hash_path(const char* path);
void asset_decrypt(uint8_t* buf, size_t* len, const char* url);

#endif
