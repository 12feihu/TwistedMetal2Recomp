/*
 * Twisted Metal 2 debug GUI.
 *
 * A separate process on purpose. The cheat engine lives inside the game as a
 * mod plugin, and its VBlank callback runs on the emulation thread -- a GUI
 * hosted there would have to create a window and juggle the game's OpenGL
 * context from inside guest execution. Keeping the panel in its own process
 * means nothing it does can disturb the game's renderer, event loop or
 * timing, and a crash here cannot take the game down.
 *
 * Talks to src/mods/tm2_debug_ipc.c over a line protocol on localhost:4371.
 *
 *   LIST                 OK <n>, then rows: idx \t on \t region \t cat \t name \t id
 *   SET <index> <0|1>    OK
 *   CLEAR                OK
 *   STATUS               OK <active> <total>
 *
 * Run with --selftest to exercise the whole path without a human: it
 * connects, toggles a cheat, verifies the change is reflected, restores the
 * previous state and exits with a status code.
 */

#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_opengl3.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#if defined(_WIN32)
#  include <winsock2.h>
#  include <ws2tcpip.h>
   typedef SOCKET sock_t;
#  define BAD_SOCK INVALID_SOCKET
#  define close_sock closesocket
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
   typedef int sock_t;
#  define BAD_SOCK (-1)
#  define close_sock close
#endif

namespace {

enum { PARAM_NONE = 0, PARAM_RANGE = 1, PARAM_CHOICE = 2 };

struct Cheat {
    int         index = 0;
    bool        enabled = false;
    std::string region;
    std::string category;
    std::string name;
    std::string id;
    // Modifier cheats carry a placeholder value in the published GameShark
    // code, so they get a picker rather than a bare checkbox.
    int         param_kind = PARAM_NONE;
    long        value = 0;
    long        vmin = 0;
    long        vmax = 0;
    std::string param_label;
    std::vector<std::string> choices;   // fetched lazily, only when expanded
    bool        choices_fetched = false;
};

int g_port = 4371;

sock_t connect_game()
{
    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == BAD_SOCK) return BAD_SOCK;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<unsigned short>(g_port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        close_sock(s);
        return BAD_SOCK;
    }
    return s;
}

/*
 * One request, one reply, one connection. The game's server is polled at
 * VBlank so a persistent socket buys nothing, and a fresh connection per
 * request means a dropped game never leaves the GUI wedged.
 */
bool request(const std::string &line, std::string *out)
{
    out->clear();
    sock_t s = connect_game();
    if (s == BAD_SOCK) return false;

    std::string msg = line + "\n";
    if (send(s, msg.data(), static_cast<int>(msg.size()), 0) < 0) {
        close_sock(s);
        return false;
    }

    // The server closes nothing, so read until it stops producing for a
    // moment. Replies are small and arrive in one or two segments.
#if defined(_WIN32)
    DWORD tv = 400;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&tv),
               sizeof(tv));
#else
    timeval tv{0, 400 * 1000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    char buf[8192];
    for (;;) {
        int n = static_cast<int>(recv(s, buf, sizeof(buf), 0));
        if (n <= 0) break;
        out->append(buf, static_cast<size_t>(n));
        // A LIST reply is "OK <n>\n" plus n rows; stop once we have them all.
        if (out->size() > 2 && out->compare(0, 3, "OK ") == 0) {
            size_t nl = out->find('\n');
            if (nl != std::string::npos) {
                int want = atoi(out->c_str() + 3);
                if (want > 0) {
                    long rows = std::count(out->begin() + static_cast<long>(nl),
                                           out->end(), '\n') - 1;
                    if (rows >= want) break;
                } else {
                    break;
                }
            }
        } else if (!out->empty() && out->find('\n') != std::string::npos) {
            break;
        }
    }
    close_sock(s);
    return !out->empty();
}

std::vector<Cheat> fetch_list(bool *ok)
{
    std::vector<Cheat> list;
    std::string reply;
    *ok = request("LIST", &reply);
    if (!*ok) return list;

    size_t pos = reply.find('\n');
    if (pos == std::string::npos) return list;
    pos++;
    while (pos < reply.size()) {
        size_t eol = reply.find('\n', pos);
        if (eol == std::string::npos) eol = reply.size();
        std::string row = reply.substr(pos, eol - pos);
        pos = eol + 1;

        std::vector<std::string> f;
        size_t start = 0;
        for (;;) {
            size_t tab = row.find('\t', start);
            if (tab == std::string::npos) { f.push_back(row.substr(start)); break; }
            f.push_back(row.substr(start, tab - start));
            start = tab + 1;
        }
        if (f.size() < 6) continue;
        Cheat c;
        c.index = atoi(f[0].c_str());
        c.enabled = f[1] == "1";
        c.region = f[2];
        c.category = f[3];
        c.name = f[4];
        c.id = f[5];
        if (f.size() >= 11) {
            c.param_kind = atoi(f[6].c_str());
            c.value = atol(f[7].c_str());
            c.vmin = atol(f[8].c_str());
            c.vmax = atol(f[9].c_str());
            c.param_label = f[10];
        }
        list.push_back(std::move(c));
    }
    return list;
}

int run_selftest()
{
    std::printf("tm2-debug-gui selftest: connecting to 127.0.0.1:%d\n", g_port);
    bool ok = false;
    std::vector<Cheat> list = fetch_list(&ok);
    if (!ok || list.empty()) {
        std::printf("  FAIL: no cheat list (is the game running with the "
                    "debug mod enabled?)\n");
        return 1;
    }
    std::printf("  LIST ok: %zu cheats\n", list.size());

    const Cheat &c = list[0];
    bool before = c.enabled;
    std::string reply;

    request("SET " + std::to_string(c.index) + " " + (before ? "0" : "1"),
            &reply);
    std::vector<Cheat> after = fetch_list(&ok);
    if (!ok || after.size() != list.size()) {
        std::printf("  FAIL: list changed shape after SET\n");
        return 1;
    }
    if (after[0].enabled == before) {
        std::printf("  FAIL: '%s' did not change state\n", c.name.c_str());
        return 1;
    }
    std::printf("  SET ok: '%s' %d -> %d\n", c.name.c_str(),
                before ? 1 : 0, after[0].enabled ? 1 : 0);

    request("SET " + std::to_string(c.index) + " " + (before ? "1" : "0"),
            &reply);
    request("STATUS", &reply);
    std::printf("  STATUS: %s", reply.c_str());
    std::printf("  PASS\n");
    return 0;
}

}  // namespace

/* Render one frame to a BMP and exit. Lets the panel's appearance be checked
 * without a human at the screen, which is otherwise impossible for a GUI. */
void save_frame_bmp(SDL_Window *win, const char *path)
{
    int w, h;
    SDL_GetWindowSizeInPixels(win, &w, &h);
    std::vector<unsigned char> px((size_t)w * (size_t)h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());

    // glReadPixels is bottom-up; SDL surfaces are top-down.
    std::vector<unsigned char> flipped((size_t)w * (size_t)h * 4);
    for (int y = 0; y < h; y++)
        std::memcpy(&flipped[(size_t)y * (size_t)w * 4],
                    &px[(size_t)(h - 1 - y) * (size_t)w * 4],
                    (size_t)w * 4);

    SDL_Surface *surf = SDL_CreateSurfaceFrom(
        w, h, SDL_PIXELFORMAT_ABGR8888, flipped.data(), w * 4);
    if (surf) {
        if (!SDL_SaveBMP(surf, path))
            std::fprintf(stderr, "SDL_SaveBMP failed: %s\n", SDL_GetError());
        else
            std::printf("wrote %s (%dx%d)\n", path, w, h);
        SDL_DestroySurface(surf);
    }
}

int main(int argc, char **argv)
{
    bool selftest = false;
    const char *shot_path = nullptr;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--selftest") == 0) selftest = true;
        else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc)
            shot_path = argv[++i];
        else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc)
            g_port = atoi(argv[++i]);
    }
    if (const char *env = getenv("TM2_DEBUG_GUI_PORT"))
        if (*env) g_port = atoi(env);

#if defined(_WIN32)
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    if (selftest) return run_selftest();

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window *win = SDL_CreateWindow("Twisted Metal 2 - Debug", 520, 760,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!win) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GLContext gl = SDL_GL_CreateContext(win);
    SDL_GL_MakeCurrent(win, gl);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForOpenGL(win, gl);
    ImGui_ImplOpenGL3_Init("#version 130");

    std::vector<Cheat> cheats;
    bool connected = false;
    Uint64 last_poll = 0;
    char filter[128] = {0};

    bool running = true;
    int frames = 0;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL3_ProcessEvent(&ev);
            if (ev.type == SDL_EVENT_QUIT) running = false;
            if (ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                ev.window.windowID == SDL_GetWindowID(win))
                running = false;
        }

        // Re-poll twice a second so the panel follows changes made with the
        // in-game F1 menu, and reconnects on its own when the game restarts.
        Uint64 now = SDL_GetTicks();
        if (last_poll == 0 || now - last_poll > 500) {
            last_poll = now;
            bool ok = false;
            std::vector<Cheat> fresh = fetch_list(&ok);
            connected = ok && !fresh.empty();
            if (connected) {
                // Merge rather than replace: choice labels are fetched lazily
                // with a separate request, and a wholesale swap would discard
                // them every refresh and re-request forever.
                for (size_t i = 0; i < fresh.size(); i++) {
                    if (i < cheats.size() && cheats[i].id == fresh[i].id) {
                        fresh[i].choices = std::move(cheats[i].choices);
                        fresh[i].choices_fetched = cheats[i].choices_fetched;
                    }
                }
                cheats = std::move(fresh);
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport *vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("tm2debug", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        if (!connected) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
                               "Not connected to the game.");
            ImGui::TextWrapped(
                "Start Twisted Metal 2 with the tm2.debug mod enabled. "
                "Listening on 127.0.0.1:%d.", g_port);
        } else {
            int active = 0;
            for (const Cheat &c : cheats) if (c.enabled) active++;
            ImGui::Text("%d cheats, %d active", (int)cheats.size(), active);
            ImGui::SameLine();
            if (ImGui::Button("All off")) {
                std::string r;
                request("CLEAR", &r);
                for (Cheat &c : cheats) c.enabled = false;
            }
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##filter", "filter...", filter,
                                     sizeof(filter));
            ImGui::Separator();

            std::string needle = filter;
            std::transform(needle.begin(), needle.end(), needle.begin(),
                           [](unsigned char ch) { return (char)tolower(ch); });

            // The server sends cheats in source order, which is alphabetical
            // by name, so categories are interleaved. Group them here or every
            // row grows its own header.
            std::vector<std::string> order;
            std::map<std::string, std::vector<Cheat *>> groups;
            for (Cheat &c : cheats) {
                if (!needle.empty()) {
                    std::string hay = c.name + " " + c.category + " " + c.id;
                    std::transform(hay.begin(), hay.end(), hay.begin(),
                                   [](unsigned char ch) { return (char)tolower(ch); });
                    if (hay.find(needle) == std::string::npos) continue;
                }
                if (!groups.count(c.category)) order.push_back(c.category);
                groups[c.category].push_back(&c);
            }

            ImGui::BeginChild("list");
            for (const std::string &cat : order) {
                std::vector<Cheat *> &rows = groups[cat];
                char header[128];
                snprintf(header, sizeof(header), "%s (%d)", cat.c_str(),
                         (int)rows.size());
                if (!ImGui::CollapsingHeader(header,
                                             ImGuiTreeNodeFlags_DefaultOpen))
                    continue;
                for (Cheat *cp : rows) {
                Cheat &c = *cp;
                ImGui::PushID(c.index);
                bool on = c.enabled;
                if (ImGui::Checkbox(c.name.c_str(), &on)) {
                    std::string r;
                    if (request("SET " + std::to_string(c.index) + " " +
                                (on ? "1" : "0"), &r))
                        c.enabled = on;
                }
                // Region tag: RAM is safe any time, EXE patches an
                // instruction, LVL only means anything inside a loaded level.
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 30.0f);
                ImVec4 col = c.region == "LVL" ? ImVec4(1.0f, 0.6f, 0.2f, 1.0f)
                           : c.region == "EXE" ? ImVec4(0.5f, 0.8f, 1.0f, 1.0f)
                                               : ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
                ImGui::TextColored(col, "%s", c.region.c_str());
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        c.region == "LVL"
                          ? "Streamed level data: only meaningful once the\n"
                            "matching level is loaded. Can corrupt memory\n"
                            "if used at the wrong time."
                          : c.region == "EXE"
                          ? "Patches an instruction in the executable.\n"
                            "Restored when switched off."
                          : "Runtime state. Safe at any time.");
                }
                // Modifier cheats get a picker on the following line. The
                // published GameShark value for these is a placeholder, so a
                // checkbox alone would always write the same useless number.
                if (c.param_kind != PARAM_NONE) {
                    ImGui::Indent(24.0f);
                    ImGui::SetNextItemWidth(-60.0f);
                    if (c.param_kind == PARAM_CHOICE) {
                        if (!c.choices_fetched) {
                            std::string r;
                            if (request("CHOICES " + std::to_string(c.index), &r)) {
                                size_t nl = r.find('\n');
                                if (nl != std::string::npos) {
                                    size_t p = nl + 1;
                                    while (p < r.size()) {
                                        size_t e = r.find('\n', p);
                                        if (e == std::string::npos) e = r.size();
                                        if (e > p) c.choices.push_back(r.substr(p, e - p));
                                        p = e + 1;
                                    }
                                }
                            }
                            c.choices_fetched = true;
                        }
                        int sel = (int)c.value;
                        if (sel < 0 || sel >= (int)c.choices.size()) sel = 0;
                        const char *preview = c.choices.empty()
                                            ? "?" : c.choices[sel].c_str();
                        if (ImGui::BeginCombo(c.param_label.c_str(), preview)) {
                            for (int k = 0; k < (int)c.choices.size(); k++) {
                                bool is_sel = (k == sel);
                                if (ImGui::Selectable(c.choices[k].c_str(), is_sel)) {
                                    std::string r;
                                    if (request("SETVAL " + std::to_string(c.index) +
                                                " " + std::to_string(k), &r))
                                        c.value = k;
                                }
                                if (is_sel) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                    } else {
                        int v = (int)c.value;
                        if (ImGui::DragInt(c.param_label.c_str(), &v, 1.0f,
                                           (int)c.vmin, (int)c.vmax)) {
                            std::string r;
                            if (request("SETVAL " + std::to_string(c.index) +
                                        " " + std::to_string(v), &r))
                                c.value = v;
                        }
                    }
                    ImGui::Unindent(24.0f);
                }
                ImGui::PopID();
                }
            }
            ImGui::EndChild();
        }

        ImGui::End();
        ImGui::Render();

        int w, h;
        SDL_GetWindowSizeInPixels(win, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.09f, 0.09f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (shot_path && frames >= 2) {   /* let the layout settle first */
            save_frame_bmp(win, shot_path);
            running = false;
        }
        SDL_GL_SwapWindow(win);
        frames++;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
