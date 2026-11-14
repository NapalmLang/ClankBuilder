#include "fire_zstd.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zstd.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <fcntl.h>
#  include <io.h>
#  include <wchar.h>
#else
#  include <unistd.h>
#endif

#if defined(_MSC_VER)
#  define FIRE_THREAD_LOCAL __declspec(thread)
#else
#  define FIRE_THREAD_LOCAL _Thread_local
#endif

#define FIRE_IO_CHUNK (128u * 1024u)
static FIRE_THREAD_LOCAL char g_last_error[512];

static int32_t set_error(int32_t code, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(g_last_error, sizeof(g_last_error), format, args);
    va_end(args);
    return code;
}

static void clear_error(void) {
    g_last_error[0] = '\0';
}

int32_t fire_zstd_abi_version(void) { return 1; }
int32_t fire_zstd_version_number(void) { return (int32_t)ZSTD_versionNumber(); }
const char *fire_zstd_last_error(void) { return g_last_error; }

#if defined(_WIN32)
static wchar_t *utf8_to_wide(const char *value) {
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, NULL, 0);
    if (length <= 0) return NULL;
    wchar_t *result = (wchar_t *)malloc((size_t)length * sizeof(wchar_t));
    if (!result) return NULL;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, result, length) != length) {
        free(result);
        return NULL;
    }
    return result;
}

static FILE *open_input(const char *path) {
    wchar_t *wide = utf8_to_wide(path);
    if (!wide) return NULL;
    FILE *result = NULL;
    (void)_wfopen_s(&result, wide, L"rb");
    free(wide);
    return result;
}

static int remove_temp(const char *path) {
    wchar_t *wide = utf8_to_wide(path);
    if (!wide) return -1;
    BOOL ok = DeleteFileW(wide);
    free(wide);
    return ok ? 0 : -1;
}

static int commit_temp(const char *source, const char *destination) {
    wchar_t *src = utf8_to_wide(source);
    wchar_t *dst = utf8_to_wide(destination);
    if (!src || !dst) {
        free(src);
        free(dst);
        return -1;
    }
    BOOL ok = MoveFileExW(src, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    free(src);
    free(dst);
    return ok ? 0 : -1;
}

static FILE *open_temporary(const char *destination, char **temporary_path) {
    static volatile LONG counter = 0;
    size_t length = strlen(destination);
    if (length > SIZE_MAX - 80) return NULL;
    char *name = (char *)malloc(length + 80);
    if (!name) return NULL;
    for (unsigned int attempt = 0; attempt < 64; attempt++) {
        unsigned long sequence = (unsigned long)InterlockedIncrement(&counter);
        int count = snprintf(name, length + 80, "%s.fire-tmp-%lu-%lu", destination,
                             (unsigned long)GetCurrentProcessId(), sequence);
        if (count < 0 || (size_t)count >= length + 80) break;
        wchar_t *wide = utf8_to_wide(name);
        if (!wide) break;
        HANDLE handle = CreateFileW(wide, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, NULL);
        free(wide);
        if (handle == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_EXISTS || GetLastError() == ERROR_ALREADY_EXISTS) continue;
            break;
        }
        int fd = _open_osfhandle((intptr_t)handle, _O_WRONLY | _O_BINARY);
        if (fd < 0) {
            CloseHandle(handle);
            remove_temp(name);
            break;
        }
        FILE *file = _fdopen(fd, "wb");
        if (!file) {
            _close(fd);
            remove_temp(name);
            break;
        }
        *temporary_path = name;
        return file;
    }
    free(name);
    return NULL;
}
#else
static FILE *open_input(const char *path) { return fopen(path, "rb"); }
static int remove_temp(const char *path) { return unlink(path); }
static int commit_temp(const char *source, const char *destination) {
    return rename(source, destination);
}

static FILE *open_temporary(const char *destination, char **temporary_path) {
    size_t length = strlen(destination);
    if (length > SIZE_MAX - 20) return NULL;
    char *name = (char *)malloc(length + 20);
    if (!name) return NULL;
    snprintf(name, length + 20, "%s.fire-tmp-XXXXXX", destination);
    int fd = mkstemp(name);
    if (fd < 0) {
        free(name);
        return NULL;
    }
    FILE *file = fdopen(fd, "wb");
    if (!file) {
        close(fd);
        unlink(name);
        free(name);
        return NULL;
    }
    *temporary_path = name;
    return file;
}
#endif

static int32_t validate(const char *source, const char *destination) {
    if (!source || !destination || !source[0] || !destination[0])
        return set_error(FIRE_ZSTD_INVALID_ARGUMENT, "Source and destination paths must be nonempty");
    if (strcmp(source, destination) == 0)
        return set_error(FIRE_ZSTD_INVALID_ARGUMENT, "Source and destination paths cannot be identical");
    return FIRE_ZSTD_OK;
}

static int32_t write_all(FILE *file, const void *data, size_t count) {
    if (count && fwrite(data, 1, count, file) != count)
        return set_error(FIRE_ZSTD_IO_ERROR, "Failed to write output: %s", strerror(errno));
    return FIRE_ZSTD_OK;
}

static int32_t finish_or_discard(FILE *output, char *temporary, const char *destination, int32_t status) {
    if (status == FIRE_ZSTD_OK) {
        /* Only publish complete, successfully synchronized files. */
        if (fflush(output) != 0 || ferror(output)) {
            status = set_error(FIRE_ZSTD_IO_ERROR, "Could not flush output file");
        }
#if defined(_WIN32)
        if (status == FIRE_ZSTD_OK && _commit(_fileno(output)) != 0)
#else
        if (status == FIRE_ZSTD_OK && fsync(fileno(output)) != 0)
#endif
            status = set_error(FIRE_ZSTD_IO_ERROR, "Could not synchronize output file");
    }
    if (fclose(output) != 0 && status == FIRE_ZSTD_OK)
        status = set_error(FIRE_ZSTD_IO_ERROR, "Could not close output file");
    if (status == FIRE_ZSTD_OK && commit_temp(temporary, destination) != 0)
        status = set_error(FIRE_ZSTD_IO_ERROR, "Could not rename temporary output file");
    if (status != FIRE_ZSTD_OK) remove_temp(temporary);
    free(temporary);
    return status;
}

int32_t fire_zstd_compress_file(const char *source, const char *destination, int32_t level) {
    clear_error();
    int32_t status = validate(source, destination);
    if (status) return status;
    if (level < ZSTD_minCLevel() || level > ZSTD_maxCLevel())
        return set_error(FIRE_ZSTD_INVALID_ARGUMENT, "Compression level outside zstd-supported range");

    FILE *input = open_input(source);
    if (!input) return set_error(FIRE_ZSTD_IO_ERROR, "Cannot open source file");

    ZSTD_CCtx *context = ZSTD_createCCtx();
    unsigned char *in_data = (unsigned char *)malloc(FIRE_IO_CHUNK);
    unsigned char *out_data = (unsigned char *)malloc(FIRE_IO_CHUNK);
    char *temporary = NULL;
    FILE *output = NULL;
    if (!context || !in_data || !out_data) {
        status = set_error(FIRE_ZSTD_OUT_OF_MEMORY, "Could not allocate compression buffers");
        goto cleanup;
    }

    size_t code = ZSTD_CCtx_setParameter(context, ZSTD_c_compressionLevel, level);
    if (!ZSTD_isError(code)) code = ZSTD_CCtx_setParameter(context, ZSTD_c_checksumFlag, 1);
    if (ZSTD_isError(code)) {
        status = set_error(FIRE_ZSTD_CODEC_ERROR, "Zstd configuration: %s", ZSTD_getErrorName(code));
        goto cleanup;
    }

    output = open_temporary(destination, &temporary);
    if (!output) {
        status = set_error(FIRE_ZSTD_IO_ERROR, "Cannot create temporary output file");
        goto cleanup;
    }

    for (;;) {
        size_t read_count = fread(in_data, 1, FIRE_IO_CHUNK, input);
        if (ferror(input)) {
            status = set_error(FIRE_ZSTD_IO_ERROR, "Could not read input file");
            break;
        }
        ZSTD_EndDirective mode = feof(input) ? ZSTD_e_end : ZSTD_e_continue;
        ZSTD_inBuffer in = {in_data, read_count, 0};
        size_t remaining;
        do {
            ZSTD_outBuffer out = {out_data, FIRE_IO_CHUNK, 0};
            remaining = ZSTD_compressStream2(context, &out, &in, mode);
            if (ZSTD_isError(remaining)) {
                status = set_error(FIRE_ZSTD_CODEC_ERROR, "Zstd compression: %s", ZSTD_getErrorName(remaining));
                break;
            }
            status = write_all(output, out_data, out.pos);
            if (status) break;
        } while (in.pos < in.size || (mode == ZSTD_e_end && remaining != 0));
        if (status || mode == ZSTD_e_end) break;
    }

cleanup:
    if (fclose(input) != 0 && status == FIRE_ZSTD_OK)
        status = set_error(FIRE_ZSTD_IO_ERROR, "Could not close source file");
    if (output) status = finish_or_discard(output, temporary, destination, status);
    free(in_data);
    free(out_data);
    ZSTD_freeCCtx(context);
    return status;
}

int32_t fire_zstd_decompress_file(const char *source, const char *destination,
                                 uint64_t max_output_bytes) {
    clear_error();
    int32_t status = validate(source, destination);
    if (status) return status;
    if (max_output_bytes == 0)
        return set_error(FIRE_ZSTD_INVALID_ARGUMENT, "Decompression size limit must be positive");

    FILE *input = open_input(source);
    if (!input) return set_error(FIRE_ZSTD_IO_ERROR, "Cannot open compressed input file");

    ZSTD_DCtx *context = ZSTD_createDCtx();
    unsigned char *in_data = (unsigned char *)malloc(FIRE_IO_CHUNK);
    unsigned char *out_data = (unsigned char *)malloc(FIRE_IO_CHUNK);
    char *temporary = NULL;
    FILE *output = NULL;
    uint64_t produced = 0;
    int end_of_frame = 0;
    if (!context || !in_data || !out_data) {
        status = set_error(FIRE_ZSTD_OUT_OF_MEMORY, "Could not allocate decompression buffers");
        goto cleanup;
    }
    output = open_temporary(destination, &temporary);
    if (!output) {
        status = set_error(FIRE_ZSTD_IO_ERROR, "Cannot create temporary output file");
        goto cleanup;
    }

    for (;;) {
        size_t read_count = fread(in_data, 1, FIRE_IO_CHUNK, input);
        if (ferror(input)) {
            status = set_error(FIRE_ZSTD_IO_ERROR, "Could not read compressed input");
            break;
        }
        if (!read_count) {
            if (!end_of_frame)
                status = set_error(FIRE_ZSTD_INVALID_STREAM, "Empty or truncated Zstd stream");
            break;
        }
        ZSTD_inBuffer in = {in_data, read_count, 0};
        while (in.pos < in.size) {
            size_t previous_pos = in.pos;
            ZSTD_outBuffer out = {out_data, FIRE_IO_CHUNK, 0};
            size_t remaining = ZSTD_decompressStream(context, &out, &in);
            if (ZSTD_isError(remaining)) {
                status = set_error(FIRE_ZSTD_CODEC_ERROR, "Zstd decompression: %s", ZSTD_getErrorName(remaining));
                break;
            }
            if ((uint64_t)out.pos > max_output_bytes - produced) {
                status = set_error(FIRE_ZSTD_LIMIT_EXCEEDED, "Decompressed file exceeds configured limit");
                break;
            }
            status = write_all(output, out_data, out.pos);
            if (status) break;
            produced += (uint64_t)out.pos;
            end_of_frame = (remaining == 0);
            if (in.pos == previous_pos && out.pos == 0) {
                status = set_error(FIRE_ZSTD_INVALID_STREAM, "Zstd decoder made no progress");
                break;
            }
        }
        if (status) break;
    }

cleanup:
    if (fclose(input) != 0 && status == FIRE_ZSTD_OK)
        status = set_error(FIRE_ZSTD_IO_ERROR, "Could not close compressed input file");
    if (output) status = finish_or_discard(output, temporary, destination, status);
    free(in_data);
    free(out_data);
    ZSTD_freeDCtx(context);
    return status;
}
