#include "vendor/vec/vec.h"
#include <stddef.h>

#pragma once

#define containerof(ptr, type) ((type*)((char*)(ptr) - offsetof(type, ref)))
#define LOVR_COLOR(r, g, b, a) ((a << 0) | (b << 8) | (g << 16) | (r << 24))
#define LOVR_COLOR_R(c) (c >> 24 & 0xff)
#define LOVR_COLOR_G(c) (c >> 16 & 0xff)
#define LOVR_COLOR_B(c) (c >> 8  & 0xff)
#define LOVR_COLOR_A(c) (c >> 0  & 0xff)

#define LOVR_PATH_MAX 1024

typedef vec_t(unsigned int) vec_uint_t;

typedef struct ref {
  void (*free)(const struct ref* ref);
  int count;
} Ref;

void error(const char* format, ...);
void lovrSleep(double seconds);
void* lovrAlloc(size_t size, void (*destructor)(const Ref* ref));
void lovrRetain(const Ref* ref);
void lovrRelease(const Ref* ref);
size_t utf8_decode(const char *s, const char *e, unsigned *pch);
int mkdir_p(const char* path);
void path_join(char* dest, const char* p1, const char* p2);
void path_normalize(char* path);
