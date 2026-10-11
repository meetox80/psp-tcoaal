#ifndef TB_UTIL_H
#define TB_UTIL_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint8_t* data;
    size_t len;
    size_t cap;
} Buf;

void* xmalloc(size_t n);
void* xcalloc(size_t n, size_t size);
void* xrealloc(void* p, size_t n);
char* xstrdup(const char* s);
char* xstrndup(const char* s, size_t n);
char* str_fmt(const char* fmt, ...);

void buf_reserve(Buf* b, size_t extra);
void buf_put(Buf* b, const void* p, size_t n);
void buf_u8(Buf* b, uint8_t v);
void buf_u16(Buf* b, uint16_t v);
void buf_u32(Buf* b, uint32_t v);
void buf_zero(Buf* b, size_t n);
void buf_free(Buf* b);

FILE* file_open(const char* path, const char* mode);
int file_read(const char* path, Buf* out);
int file_write(const char* path, const void* data, size_t len);
int file_exists(const char* path);
int dir_exists(const char* path);
int make_dirs(const char* path);
char** dir_list(const char* path, int* count);
void str_list_free(char** list, int count);
char* path_join(const char* a, const char* b);

void log_msg(const char* fmt, ...);
void fatal(const char* fmt, ...);
void set_fatal_hook(void (*fn)(const char* msg));

typedef void (*JobFn)(void* ctx, int index);
void run_jobs(JobFn fn, void* ctx, int count, int threads);
int cpu_count(void);
void sleep_ms(int ms);

#ifdef _WIN32
#include <wchar.h>
wchar_t* utf8_to_wide(const char* s);
char* wide_to_utf8(const wchar_t* w);
#endif

#endif
