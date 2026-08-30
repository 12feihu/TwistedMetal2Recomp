/*
 * Twisted Metal 2 debug menu + cheat engine.
 *
 * All custom code for this project lives here rather than in the recompiled
 * game C, which stays byte-for-byte vanilla. This file is compiled into
 * psx-runtime through psxrecomp_add_game_runtime(EXTRAS_SOURCES ...) and
 * registers a trusted plugin the .psxmod package selects by id.
 *
 * The cheat table is generated from the GameShark list by tools/gs_import.py;
 * see docs/RAM_MAP.md for what the addresses mean and how far to trust them.
 *
 * ---------------------------------------------------------------------------
 * Controls (F1 opens; everything else is chosen to avoid the pad bindings in
 * keybinds.ini, so the game never sees a keystroke meant for the menu):
 *
 *   F1          open / close the menu
 *   PageUp/Dn   previous / next cheat
 *   Home/End    previous / next category
 *   Insert      toggle the selected cheat
 *   Delete      turn every enabled cheat off
 *
 * ---------------------------------------------------------------------------
 * Two things about applying GameShark codes to a *recompiled* game:
 *
 * 1. Codes that patch an instruction cannot simply poke guest RAM. The game
 *    is statically recompiled, so the native code for that address already
 *    exists and would never see the write. psx_mod_write_code_word() exists
 *    for exactly this: it replaces the instruction and routes the address
 *    through the runtime's executable-RAM path. Image-region ops therefore go
 *    through a read-modify-write of the containing word.
 *
 * 2. Everything is applied every VBlank, which is what a real GameShark does.
 *    A one-shot write would be undone by the game's own logic on the next
 *    frame.
 *
 * Nothing here is gated by region: low-region cheats address streamed level
 * data and are only meaningful once the matching level is loaded, and using
 * one at the wrong moment can corrupt whatever occupies that memory instead.
 * The menu shows the region so the choice is visible, but does not block it.
 */

#include "tm2_cheat_table.h"

#include "host_osd.h"
#include "mod_plugins.h"
#include "psx_sdl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TM2_PLUGIN_ID "tm2.debug.menu"

/* One bit per cheat. */
static uint8_t s_enabled[(TM2_CHEAT_COUNT + 7) / 8];
static int     s_menu_open;
static int     s_cursor;
static int     s_active_count;

/* Edge detection for our own keys; SDL scancodes we poll directly rather than
 * consuming SDL events, because the runtime owns the event loop. */
static Uint8 s_key_prev[SDL_NUM_SCANCODES];

static int cheat_enabled(int i)
{
    return (s_enabled[i >> 3] >> (i & 7)) & 1;
}

static void cheat_set(int i, int on)
{
    if (on) s_enabled[i >> 3] |= (uint8_t)(1u << (i & 7));
    else    s_enabled[i >> 3] &= (uint8_t)~(1u << (i & 7));
}

static const char *region_tag(uint8_t region)
{
    switch (region) {
    case TM2_REGION_IMAGE: return "EXE";
    case TM2_REGION_LOW:   return "LVL";
    default:               return "RAM";
    }
}

/* ------------------------------------------------------------------ apply */

static void write_half(const Tm2CheatOp *op)
{
    if (op->region == TM2_REGION_IMAGE) {
        /* Recompiled code: patch the whole word through the executable-RAM
         * path so the native side actually changes. */
        uint32_t word_addr = op->addr & ~3u;
        uint32_t cur = psx_mod_read_word(word_addr);
        uint32_t next = (op->addr & 2u)
                      ? ((cur & 0x0000FFFFu) | ((uint32_t)op->value << 16))
                      : ((cur & 0xFFFF0000u) | op->value);
        if (next != cur)
            psx_mod_write_code_word(word_addr, next);
    } else {
        psx_mod_write_half(op->addr, op->value);
    }
}

static void apply_cheat(const Tm2Cheat *c)
{
    uint16_t i = 0;
    while (i < c->op_count) {
        const Tm2CheatOp *op = &tm2_cheat_ops[c->first_op + i];
        if (op->kind == TM2_OP_IF_EQ16) {
            /* GameShark D0: run the next op only if the test passes. */
            if (psx_mod_read_half(op->addr) != op->value) {
                i += 2;
                continue;
            }
            i += 1;
            continue;
        }
        if (op->kind == TM2_OP_WRITE8)
            psx_mod_write_byte(op->addr, (uint8_t)(op->value & 0xFF));
        else
            write_half(op);
        i += 1;
    }
}

/* ------------------------------------------------------------------- menu */

static int key_pressed(SDL_Scancode sc, const Uint8 *keys)
{
    int now = keys[sc] ? 1 : 0;
    int was = s_key_prev[sc];
    s_key_prev[sc] = (Uint8)now;
    return now && !was;
}

static void recount_active(void)
{
    s_active_count = 0;
    for (int i = 0; i < TM2_CHEAT_COUNT; i++)
        if (cheat_enabled(i)) s_active_count++;
}

static void step_category(int dir)
{
    const char *cur = tm2_cheats[s_cursor].category;
    int i = s_cursor;
    for (int n = 0; n < TM2_CHEAT_COUNT; n++) {
        i = (i + dir + TM2_CHEAT_COUNT) % TM2_CHEAT_COUNT;
        if (strcmp(tm2_cheats[i].category, cur) != 0) {
            /* Land on the first entry of that category, not the last. */
            if (dir < 0) {
                const char *want = tm2_cheats[i].category;
                while (strcmp(tm2_cheats[(i - 1 + TM2_CHEAT_COUNT)
                                         % TM2_CHEAT_COUNT].category,
                              want) == 0)
                    i = (i - 1 + TM2_CHEAT_COUNT) % TM2_CHEAT_COUNT;
            }
            s_cursor = i;
            return;
        }
    }
}

static void redraw(void)
{
    if (!s_menu_open) {
        /* Leave a small reminder while cheats are live, so a stray toggle is
         * never invisible. */
        if (s_active_count > 0) {
            char line[64];
            snprintf(line, sizeof(line), "F1  %d cheat%s active",
                     s_active_count, s_active_count == 1 ? "" : "s");
            host_osd_set_status(line);
        } else {
            host_osd_set_status(NULL);
        }
        return;
    }

    /* The OSD is a single 64-character line, so the menu is a one-line
     * selector rather than a list. */
    const Tm2Cheat *c = &tm2_cheats[s_cursor];
    char line[64];
    snprintf(line, sizeof(line), "%3d/%d %s %-28.28s %s",
             s_cursor + 1, TM2_CHEAT_COUNT, region_tag(c->region), c->name,
             cheat_enabled(s_cursor) ? "[ON]" : "[  ]");
    host_osd_set_status(line);
}

static void menu_tick(void)
{
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    if (!keys) return;

    int dirty = 0;

    if (key_pressed(SDL_SCANCODE_F1, keys)) {
        s_menu_open = !s_menu_open;
        dirty = 1;
    }

    /* Keep edges fresh for the navigation keys even while closed, so opening
     * the menu never replays a keypress made before it opened. */
    int pgdn = key_pressed(SDL_SCANCODE_PAGEDOWN, keys);
    int pgup = key_pressed(SDL_SCANCODE_PAGEUP, keys);
    int home = key_pressed(SDL_SCANCODE_HOME, keys);
    int end  = key_pressed(SDL_SCANCODE_END, keys);
    int ins  = key_pressed(SDL_SCANCODE_INSERT, keys);
    int del  = key_pressed(SDL_SCANCODE_DELETE, keys);

    if (s_menu_open) {
        if (pgdn) { s_cursor = (s_cursor + 1) % TM2_CHEAT_COUNT; dirty = 1; }
        if (pgup) { s_cursor = (s_cursor + TM2_CHEAT_COUNT - 1)
                               % TM2_CHEAT_COUNT; dirty = 1; }
        if (end)  { step_category(+1); dirty = 1; }
        if (home) { step_category(-1); dirty = 1; }
        if (ins) {
            cheat_set(s_cursor, !cheat_enabled(s_cursor));
            recount_active();
            dirty = 1;
        }
        if (del) {
            memset(s_enabled, 0, sizeof(s_enabled));
            recount_active();
            host_osd_push("All cheats off", 1200);
            dirty = 1;
        }
    }

    if (dirty) redraw();
}

/* -------------------------------------------------------------- callbacks */

static void tm2_debug_vblank(void)
{
    menu_tick();

    if (!psx_mod_game_started()) return;

    for (int i = 0; i < TM2_CHEAT_COUNT; i++)
        if (cheat_enabled(i))
            apply_cheat(&tm2_cheats[i]);
}

/*
 * TM2_DEBUG_CHEATS: comma-separated cheat ids to switch on at activation.
 * Ids are the slugs in mods/tm2-debug/cheats.toml, e.g.
 *
 *     set TM2_DEBUG_CHEATS=skip-intro-movie,god-mode-p1
 *
 * This exists so a debugging session is reproducible without driving the menu
 * by hand, and so the cheat engine can be exercised from a script.
 */
static void apply_env_selection(void)
{
    const char *spec = getenv("TM2_DEBUG_CHEATS");
    if (!spec || !spec[0]) return;

    int matched = 0;
    const char *p = spec;
    while (*p) {
        while (*p == ',' || *p == ' ') p++;
        const char *start = p;
        while (*p && *p != ',') p++;
        size_t len = (size_t)(p - start);
        while (len > 0 && start[len - 1] == ' ') len--;
        if (len == 0) continue;

        int found = 0;
        for (int i = 0; i < TM2_CHEAT_COUNT; i++) {
            const char *id = tm2_cheats[i].id;
            if (strlen(id) == len && strncmp(id, start, len) == 0) {
                cheat_set(i, 1);
                found = 1;
                matched++;
                break;
            }
        }
        if (!found)
            fprintf(stderr, "tm2.debug: no cheat with id '%.*s'\n",
                    (int)len, start);
    }

    recount_active();
    fprintf(stdout, "tm2.debug: %d cheat(s) enabled from TM2_DEBUG_CHEATS\n",
            matched);
}

static void tm2_debug_activate(void)
{
    memset(s_enabled, 0, sizeof(s_enabled));
    memset(s_key_prev, 0, sizeof(s_key_prev));
    s_menu_open = 0;
    s_cursor = 0;
    s_active_count = 0;
    fprintf(stdout, "tm2.debug: debug menu active (%d cheats); press F1\n",
            TM2_CHEAT_COUNT);
    apply_env_selection();
    host_osd_push("Debug menu: F1", 2500);
    redraw();
}

PSX_MOD_CONSTRUCTOR(tm2_register_debug_menu)
{
    (void)psx_mod_register_activation_plugin(TM2_PLUGIN_ID, tm2_debug_activate);
    (void)psx_mod_register_vblank_plugin(TM2_PLUGIN_ID, tm2_debug_vblank);
}
