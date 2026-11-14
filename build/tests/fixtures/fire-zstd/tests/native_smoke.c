#include "fire_zstd.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_SIZE (512u * 1024u + 137u)

static int fail(const char *message) {
    fprintf(stderr, "Fire Zstd smoke test: %s (%s)\n", message, fire_zstd_last_error());
    return 1;
}

int main(void) {
    const char *source_name = "fire-zstd-smoke-original.tmp";
    const char *packed_name = "fire-zstd-smoke-packed.tmp";
    const char *decoded_name = "fire-zstd-smoke-decoded.tmp";
    int exit_code = 1;
    unsigned char *expected = (unsigned char *)malloc(TEST_SIZE);
    unsigned char *decoded = (unsigned char *)malloc(TEST_SIZE);
    if (!expected || !decoded) return fail("allocation failed");

    for (size_t i = 0; i < TEST_SIZE; ++i) expected[i] = (unsigned char)(i * 13u + 7u);
    FILE *source = fopen(source_name, "wb");
    if (!source) goto cleanup;
    if (fwrite(expected, 1, TEST_SIZE, source) != TEST_SIZE || fclose(source) != 0)
        goto cleanup;

    if (fire_zstd_abi_version() != 1 || fire_zstd_version_number() < 10500)
        goto cleanup;
    if (fire_zstd_compress_file(source_name, packed_name, 3) != FIRE_ZSTD_OK)
        goto cleanup;
    if (fire_zstd_decompress_file(packed_name, decoded_name, TEST_SIZE + 1) != FIRE_ZSTD_OK)
        goto cleanup;

    FILE *output = fopen(decoded_name, "rb");
    if (!output) goto cleanup;
    size_t read_count = fread(decoded, 1, TEST_SIZE, output);
    int extra = fgetc(output);
    if (fclose(output) != 0 || read_count != TEST_SIZE || extra != EOF ||
        memcmp(expected, decoded, TEST_SIZE) != 0) goto cleanup;

    if (fire_zstd_decompress_file(packed_name, decoded_name, 100) != FIRE_ZSTD_LIMIT_EXCEEDED)
        goto cleanup;
    output = fopen(decoded_name, "rb");
    if (!output) goto cleanup;
    read_count = fread(decoded, 1, TEST_SIZE, output);
    if (fclose(output) != 0 || read_count != TEST_SIZE ||
        memcmp(expected, decoded, TEST_SIZE) != 0) goto cleanup;

    exit_code = 0;
    puts("Fire Zstd native smoke test passed");

cleanup:
    if (exit_code) (void)fail("unexpected compression, decompression or I/O failure");
    remove(source_name);
    remove(packed_name);
    remove(decoded_name);
    free(expected);
    free(decoded);
    return exit_code;
}
