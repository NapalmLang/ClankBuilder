#ifndef FIRE_ZSTD_H
#define FIRE_ZSTD_H

#include <stdint.h>

#if defined(_WIN32)
#  if defined(FIRE_ZSTD_EXPORTS)
#    define FIRE_ZSTD_API __declspec(dllexport)
#  else
#    define FIRE_ZSTD_API __declspec(dllimport)
#  endif
#else
#  define FIRE_ZSTD_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
    FIRE_ZSTD_OK = 0,
    FIRE_ZSTD_INVALID_ARGUMENT = 1,
    FIRE_ZSTD_IO_ERROR = 2,
    FIRE_ZSTD_CODEC_ERROR = 3,
    FIRE_ZSTD_LIMIT_EXCEEDED = 4,
    FIRE_ZSTD_INVALID_STREAM = 5,
    FIRE_ZSTD_OUT_OF_MEMORY = 6
};

FIRE_ZSTD_API int32_t fire_zstd_abi_version(void);
FIRE_ZSTD_API int32_t fire_zstd_version_number(void);
FIRE_ZSTD_API const char *fire_zstd_last_error(void);
FIRE_ZSTD_API int32_t fire_zstd_compress_file(
    const char *source_utf8, const char *destination_utf8, int32_t level);
FIRE_ZSTD_API int32_t fire_zstd_decompress_file(
    const char *source_utf8, const char *destination_utf8,
    uint64_t max_output_bytes);

#ifdef __cplusplus
}
#endif

#endif
