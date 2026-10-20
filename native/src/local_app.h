/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MUSE_LOCAL_APP_H
#define MUSE_LOCAL_APP_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define MUSE_LOCAL_SOURCE_MAX 65536u
#define MUSE_LOCAL_DRAW_MAX 512u
#define MUSE_LOCAL_TEXT_MAX 192u
#define MUSE_LOCAL_STATE_MAX 16384u
#define MUSE_LOCAL_WIDTH 1280
#define MUSE_LOCAL_HEIGHT 720
#define MUSE_LOCAL_BUTTON_COUNT 13
/* Bit positions and Lua names follow this order. */
enum muse_local_button { MUSE_LOCAL_UP, MUSE_LOCAL_DOWN, MUSE_LOCAL_LEFT,
  MUSE_LOCAL_RIGHT, MUSE_LOCAL_CROSS, MUSE_LOCAL_CIRCLE, MUSE_LOCAL_SQUARE,
  MUSE_LOCAL_TRIANGLE, MUSE_LOCAL_L1, MUSE_LOCAL_R1, MUSE_LOCAL_L2,
  MUSE_LOCAL_R2, MUSE_LOCAL_OPTIONS };
typedef struct { uint32_t down, pressed; float lx, ly, rx, ry; } muse_local_input;
enum muse_local_draw_type { MUSE_LOCAL_CLEAR, MUSE_LOCAL_RECT, MUSE_LOCAL_CIRCLE_DRAW,
  MUSE_LOCAL_LINE, MUSE_LOCAL_TEXT };
typedef struct {
  enum muse_local_draw_type type;
  float x, y, w, h, radius, thickness, font_size;
  uint8_t r, g, b, a;
  char text[MUSE_LOCAL_TEXT_MAX + 1];
} muse_local_draw_command;
typedef struct { size_t count; muse_local_draw_command commands[MUSE_LOCAL_DRAW_MAX]; } muse_local_draw_list;
typedef struct {
  size_t memory_bytes; /* 0 =>8MiB; 256KiB..16MiB accepted. */
  unsigned instructions; /* 0 =>200000; 1000..1000000 per startup/frame. */
  uint64_t seed; /* deterministic; zero is a valid seed. */
  int (*cancelled)(void *context); /* optional nonblocking; must not reenter VM. */
  void *cancel_context;
} muse_local_limits;
typedef struct {
  size_t peak_memory_bytes;
  unsigned ticks;
  size_t maximum_draw_commands;
  size_t provably_hidden_text_commands; /* across probes; not a complete contrast/legibility check */
} muse_local_validation;
typedef struct muse_local_app muse_local_app;
int muse_local_app_create(const char *source, size_t length,
    const muse_local_limits *limits, muse_local_app **out, char *error, size_t error_cap);
int muse_local_app_frame(muse_local_app *app, double dt, const muse_local_input *input,
    const muse_local_draw_list **draw, char *error, size_t error_cap);
/* Copies an opaque app-owned UTF-8 string; no filesystem access. set before first frame. */
int muse_local_app_set_state(muse_local_app *app, const char *state, size_t length);
const char *muse_local_app_get_state(const muse_local_app *app, size_t *length);
size_t muse_local_app_peak_memory(const muse_local_app *app);
void muse_local_app_destroy(muse_local_app *app);
/* Fresh VM; neutral + scripted button/axis probes; always destroys VM. ticks1..120.
 * Rejects output proven uniform in every probe; potential visibility is not an
 * app-quality/legibility proof. A blank initial frame alone does not reject. */
int muse_local_app_validate(const char *source, size_t length, const muse_local_limits *limits,
    unsigned ticks, muse_local_validation *report, char *error, size_t error_cap);
const char *muse_local_app_prompt_contract(void);
#ifdef __cplusplus
}
#endif
#endif
