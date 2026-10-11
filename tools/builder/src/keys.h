#ifndef TB_KEYS_H
#define TB_KEYS_H

#include <stddef.h>
#include <stdint.h>

#include "image.h"

Image* keys_build(const uint8_t* keys_png, size_t keys_len, const uint8_t* ttf, size_t ttf_len, const uint8_t* psp_png, size_t psp_len);

#endif
