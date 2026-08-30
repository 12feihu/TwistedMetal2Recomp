/*
 * Shared cheat state between the debug plugin and its control server.
 *
 * tm2_debug.c owns the state and the engine; tm2_debug_ipc.c drives it from
 * the external GUI over a socket. Keeping the surface this narrow means the
 * GUI can never do anything the on-screen menu cannot.
 */
#ifndef TM2_DEBUG_STATE_H
#define TM2_DEBUG_STATE_H

#include "tm2_cheat_table.h"

#ifdef __cplusplus
extern "C" {
#endif

int  tm2_cheat_count(void);
const Tm2Cheat *tm2_cheat_at(int index);
int  tm2_cheat_is_enabled(int index);
void tm2_cheat_set_enabled(int index, int enabled);
void tm2_cheat_clear_all(void);
int  tm2_cheat_active_count(void);

/* Control server, driven from the plugin's VBlank callback. Never blocks. */
void tm2_ipc_start(void);
void tm2_ipc_poll(void);
void tm2_ipc_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* TM2_DEBUG_STATE_H */
