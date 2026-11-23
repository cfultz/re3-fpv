#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <chrono>

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>

#include <safetyhook.hpp>
#include <libfpv.h>

#define FPV_RENDER_GL3_IMPLEMENTATION
#include <fpv_render_gl3.h>

#include "re3_bridge.h"

namespace {

constexpr int GLFW_PRESS           = 1;
constexpr int GLFW_CURSOR          = 0x00033001;
constexpr int GLFW_CURSOR_NORMAL   = 0x00034001;
constexpr int GLFW_CURSOR_DISABLED = 0x00034003;

constexpr int GLFW_KEY_SPACE      = 32;
constexpr int GLFW_KEY_A          = 65;
constexpr int GLFW_KEY_D          = 68;
constexpr int GLFW_KEY_R          = 82;
constexpr int GLFW_KEY_S          = 83;
constexpr int GLFW_KEY_T          = 84;
constexpr int GLFW_KEY_W          = 87;
constexpr int GLFW_KEY_ESCAPE     = 256;
constexpr int GLFW_KEY_ENTER      = 257;
constexpr int GLFW_KEY_TAB        = 258;
constexpr int GLFW_KEY_BACKSPACE  = 259;
constexpr int GLFW_KEY_RIGHT      = 262;
constexpr int GLFW_KEY_LEFT       = 263;
constexpr int GLFW_KEY_DOWN       = 264;
constexpr int GLFW_KEY_UP         = 265;
constexpr int GLFW_KEY_F7         = 296;
constexpr int GLFW_KEY_F8         = 297;
constexpr int GLFW_KEY_LSHIFT     = 340;
constexpr int GLFW_KEY_LCTRL      = 341;
constexpr int GLFW_KEY_RSHIFT     = 344;
constexpr int GLFW_KEY_RCTRL      = 345;

constexpr unsigned long FPV_STREAM_BUDGET_BYTES = 256UL * 1024UL * 1024UL;

struct KeyMap {
    int glfw_key;
    int fpv_key;
    bool prev_down;
};

KeyMap g_key_map[] = {
    {GLFW_KEY_W,         'W',               false},
    {GLFW_KEY_A,         'A',               false},
    {GLFW_KEY_S,         'S',               false},
    {GLFW_KEY_D,         'D',               false},
    {GLFW_KEY_T,         'T',               false},
    {GLFW_KEY_ENTER,     FPV_KEY_ENTER,     false},
    {GLFW_KEY_ESCAPE,    FPV_KEY_ESCAPE,    false},
    {GLFW_KEY_SPACE,     FPV_KEY_SPACE,     false},
    {GLFW_KEY_TAB,       FPV_KEY_TAB,       false},
    {GLFW_KEY_BACKSPACE, FPV_KEY_BACKSPACE, false},
    {GLFW_KEY_UP,        FPV_KEY_UP,        false},
    {GLFW_KEY_DOWN,      FPV_KEY_DOWN,      false},
    {GLFW_KEY_LEFT,      FPV_KEY_LEFT,      false},
    {GLFW_KEY_RIGHT,     FPV_KEY_RIGHT,     false},
    {GLFW_KEY_F8,        FPV_KEY_F8,        false},
    {GLFW_KEY_LSHIFT,    FPV_KEY_LSHIFT,    false},
    {GLFW_KEY_RSHIFT,    FPV_KEY_RSHIFT,    false},
    {GLFW_KEY_LCTRL,     FPV_KEY_LCTRL,     false},
    {GLFW_KEY_RCTRL,     FPV_KEY_RCTRL,     false},
};

Re3Symbols g_re3{};

SafetyHookInline g_swap_hook{};
SafetyHookInline g_game_process_hook{};
SafetyHookInline g_camera_process_hook{};
SafetyHookInline g_player_control_hook{};
SafetyHookInline g_player_centre_hook{};

bool g_fpv_ready = false;
bool g_drone_active = false;
bool g_f7_was_down = false;
bool g_r_was_down = false;
bool g_overlay_was_open = false;
bool g_mouse_was_down[3] = {false, false, false};

bool g_saved_stream_budget = false;
unsigned long g_orig_stream_budget = 0;
bool g_has_streamed_pos = false;
CVector g_last_stream_pos{};
float g_stream_timer = 0.0f;

fpv_drone* g_drone = nullptr;
fpv_frame g_last_frame{};
bool g_has_frame = false;

fpv_gl3 g_gl3{};
bool g_gl3_ready = false;
auto g_last_swap_time = std::chrono::steady_clock::now();

struct ScopedGLState {
    GLint program = 0;
    GLint vao = 0;
    GLint vbo = 0;
    GLint ebo = 0;
    GLint active_tex = 0;
    GLint tex2d = 0;

    ScopedGLState() {
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &vbo);
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &ebo);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active_tex);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex2d);
    }

    ~ScopedGLState() {
        glUseProgram(static_cast<GLuint>(program));
        glBindVertexArray(static_cast<GLuint>(vao));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(vbo));
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(ebo));
        glActiveTexture(static_cast<GLenum>(active_tex));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(tex2d));
    }
};

void set_drone_streaming_enabled(bool enabled) {
    if (!g_re3.streaming_memory_available) return;

    if (enabled) {
        if (!g_saved_stream_budget) {
            g_orig_stream_budget = *g_re3.streaming_memory_available;
            g_saved_stream_budget = true;
        }
        if (*g_re3.streaming_memory_available < FPV_STREAM_BUDGET_BYTES) {
            *g_re3.streaming_memory_available = FPV_STREAM_BUDGET_BYTES;
        }
        g_has_streamed_pos = false;
        g_stream_timer = 0.0f;
    } else {
        if (g_saved_stream_budget) {
            *g_re3.streaming_memory_available = g_orig_stream_budget;
            g_saved_stream_budget = false;
        }
        g_has_streamed_pos = false;
        g_stream_timer = 0.0f;
    }
}

void update_drone_streaming(const CVector& pos, float dt) {
    if (!g_re3.CStreaming_StreamZoneModels) return;

    g_stream_timer += dt;

    bool moved_enough = false;
    if (!g_has_streamed_pos) {
        moved_enough = true;
    } else {
        float dx = pos.x - g_last_stream_pos.x;
        float dy = pos.y - g_last_stream_pos.y;
        float dz = pos.z - g_last_stream_pos.z;
        if ((dx * dx + dy * dy + dz * dz) >= (15.0f * 15.0f)) {
            moved_enough = true;
        }
    }

    if (moved_enough || g_stream_timer >= 0.25f) {
        g_re3.CStreaming_StreamZoneModels(pos);
        g_last_stream_pos = pos;
        g_has_streamed_pos = true;
        g_stream_timer = 0.0f;
    }
}

// Raycast callback: libfpv -> re3 CWorld::ProcessLineOfSight
int re3_raycast_cb(void* /*user*/, const float origin[3], const float direction[3],
                   float max_distance, fpv_raycast_hit* out_hit) {
    if (!g_re3.CWorld_ProcessLineOfSight) return 0;

    CVector start{origin[0], origin[1], origin[2]};
    CVector end{
        origin[0] + direction[0] * max_distance,
        origin[1] + direction[1] * max_distance,
        origin[2] + direction[2] * max_distance
    };

    CColPoint col{};
    CEntity* hit_entity = nullptr;

    bool hit = g_re3.CWorld_ProcessLineOfSight(
        start, end, col, hit_entity,
        true,  // buildings
        true,  // vehicles
        true,  // peds
        true,  // objects
        true,  // dummies
        false, // see_through
        false  // ignore_some
    );

    if (!hit) return 0;

    float dx = col.point.x - start.x;
    float dy = col.point.y - start.y;
    float dz = col.point.z - start.z;
    float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

    out_hit->distance = dist;
    out_hit->normal[0] = col.normal.x;
    out_hit->normal[1] = col.normal.y;
    out_hit->normal[2] = col.normal.z;
    out_hit->material = col.surfaceB;
    out_hit->object_id = 0;
    return 1;
}

void spawn_drone_at_player() {
    if (!g_fpv_ready) return;

    if (g_drone) {
        fpv_drone_delete(g_drone);
        g_drone = nullptr;
    }

    CVector p{0.0f, 0.0f, 20.0f};
    if (g_re3.FindPlayerCoors) {
        p = g_re3.FindPlayerCoors();
    }

    fpv_drone_desc desc{};
    fpv_drone_desc_defaults(&desc);
    desc.position[0] = p.x;
    desc.position[1] = p.y + 2.0f;
    desc.position[2] = p.z + 1.5f;
    desc.pilot_position[0] = p.x;
    desc.pilot_position[1] = p.y;
    desc.pilot_position[2] = p.z + 1.0f;

    if (fpv_drone_create(&desc, &g_drone) == 0 && g_drone) {
        g_drone_active = true;
        g_has_frame = false;
        set_drone_streaming_enabled(true);
        if (g_re3.CStreaming_StreamZoneModels) {
            CVector spawn_pos{desc.position[0], desc.position[1], desc.position[2]};
            g_re3.CStreaming_StreamZoneModels(spawn_pos);
            g_last_stream_pos = spawn_pos;
            g_has_streamed_pos = true;
        }
        std::fprintf(stderr, "[re3-fpv] Drone spawned at (%.1f, %.1f, %.1f)\n",
                     desc.position[0], desc.position[1], desc.position[2]);
    } else {
        std::fprintf(stderr, "[re3-fpv] ERROR: fpv_drone_create failed.\n");
    }
}

void toggle_drone() {
    if (g_drone_active) {
        g_drone_active = false;
        g_has_frame = false;
        set_drone_streaming_enabled(false);
        if (g_drone) {
            fpv_drone_delete(g_drone);
            g_drone = nullptr;
        }
        std::fprintf(stderr, "[re3-fpv] Drone recalled.\n");
    } else {
        spawn_drone_at_player();
    }
}

void apply_drone_to_camera() {
    if (!g_drone_active || !g_has_frame || !g_re3.the_camera) return;

    const float x = g_last_frame.rotation[0];
    const float y = g_last_frame.rotation[1];
    const float z = g_last_frame.rotation[2];
    const float w = g_last_frame.rotation[3];

    // Quaternion (x,y,z,w) -> Z-up right-handed basis vectors
    const float rx = 1.0f - 2.0f * (y * y + z * z);
    const float ry = 2.0f * (x * y + w * z);
    const float rz = 2.0f * (x * z - w * y);

    const float fx = 2.0f * (x * y - w * z);
    const float fy = 1.0f - 2.0f * (x * x + z * z);
    const float fz = 2.0f * (y * z + w * x);

    const float ux = 2.0f * (x * z + w * y);
    const float uy = 2.0f * (y * z - w * x);
    const float uz = 1.0f - 2.0f * (x * x + y * y);

    const float px = g_last_frame.position[0];
    const float py = g_last_frame.position[1];
    const float pz = g_last_frame.position[2];

    CMatrix* mat = camera_matrix(g_re3.the_camera);
    mat->rx = rx; mat->ry = ry; mat->rz = rz; mat->rw = 0.0f;
    mat->fx = fx; mat->fy = fy; mat->fz = fz; mat->fw = 0.0f;
    mat->ux = ux; mat->uy = uy; mat->uz = uz; mat->uw = 0.0f;
    mat->px = px; mat->py = py; mat->pz = pz; mat->pw = 1.0f;

    RwCamera* rw_cam = camera_rw_camera(g_re3.the_camera);
    RwFrame* frame = rw_camera_get_frame(rw_cam);
    RwMatrix* rw_mat = rw_frame_get_matrix(frame);
    if (rw_mat) {
        rw_mat->right = {-rx, -ry, -rz};
        rw_mat->up    = {ux, uy, uz};
        rw_mat->at    = {fx, fy, fz};
        rw_mat->pos   = {px, py, pz};
        if (g_re3.RwFrameUpdateObjects) {
            g_re3.RwFrameUpdateObjects(frame);
        }
    }
}

void poll_input(void* window) {
    if (!g_re3.glfwGetKey || !window) return;

    bool f7_down = (g_re3.glfwGetKey(window, GLFW_KEY_F7) == GLFW_PRESS);
    if (f7_down && !g_f7_was_down) {
        toggle_drone();
    }
    g_f7_was_down = f7_down;

    bool overlay_open = (fpv_overlay_is_open() != 0);

    if (g_drone_active && !overlay_open) {
        bool r_down = (g_re3.glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS);
        if (r_down && !g_r_was_down) {
            spawn_drone_at_player();
        }
        g_r_was_down = r_down;
    }

    for (auto& km : g_key_map) {
        bool down = (g_re3.glfwGetKey(window, km.glfw_key) == GLFW_PRESS);
        if (down != km.prev_down) {
            fpv_input_key(km.fpv_key, down ? 1 : 0);
            km.prev_down = down;
        }
    }

    overlay_open = (fpv_overlay_is_open() != 0);
    if (overlay_open != g_overlay_was_open && g_re3.glfwSetInputMode) {
        g_re3.glfwSetInputMode(window, GLFW_CURSOR,
                               overlay_open ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
        g_overlay_was_open = overlay_open;
    }

    if (overlay_open) {
        if (g_re3.glfwGetCursorPos) {
            double mx = 0.0, my = 0.0;
            g_re3.glfwGetCursorPos(window, &mx, &my);
            fpv_overlay_mouse_move(static_cast<float>(mx), static_cast<float>(my));
        }
        if (g_re3.glfwGetMouseButton) {
            for (int b = 0; b < 3; ++b) {
                bool down = (g_re3.glfwGetMouseButton(window, b) == GLFW_PRESS);
                if (down != g_mouse_was_down[b]) {
                    fpv_overlay_mouse_button(b, down ? 1 : 0);
                    g_mouse_was_down[b] = down;
                }
            }
        }
    }
}

void hooked_glfwSwapBuffers(void* window) {
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - g_last_swap_time).count();
    g_last_swap_time = now;
    if (dt <= 0.0f || dt > 0.1f) dt = 1.0f / 60.0f;

    if (g_fpv_ready && window) {
        poll_input(window);

        int fb_w = 1280, fb_h = 720;
        if (g_re3.glfwGetFramebufferSize) {
            g_re3.glfwGetFramebufferSize(window, &fb_w, &fb_h);
        }
        int win_w = fb_w, win_h = fb_h;
        if (g_re3.glfwGetWindowSize) {
            g_re3.glfwGetWindowSize(window, &win_w, &win_h);
        }

        if (fb_w > 0 && fb_h > 0 && win_w > 0 && win_h > 0) {
            fpv_overlay_set_display(static_cast<float>(win_w), static_cast<float>(win_h), 1.0f);

            const fpv_draw_data* osd = nullptr;
            if (g_drone_active && g_has_frame) {
                osd = fpv_osd_draw_data(
                    &g_last_frame,
                    static_cast<float>(win_w),
                    static_cast<float>(win_h),
                    static_cast<fpv_osd_style>(0)
                );
            }
            const fpv_draw_data* ov = fpv_overlay_render(dt);

            if (osd || ov) {
                ScopedGLState gl_guard;

                if (!g_gl3_ready) {
                    int rc = fpv_gl3_init(&g_gl3);
                    if (g_gl3.program != 0) {
                        g_gl3_ready = true;
                        std::fprintf(stderr, "[re3-fpv] OpenGL 3.3 OSD/Overlay renderer initialized (rc=%d, prog=%u).\n",
                                     rc, g_gl3.program);
                    }
                }

                if (g_gl3_ready) {
                    if (osd) fpv_gl3_render(&g_gl3, osd, fb_w, fb_h);
                    if (ov)  fpv_gl3_render(&g_gl3, ov, fb_w, fb_h);
                }
            }
        }
    }

    g_swap_hook.call<void>(window);
}

void hooked_CGame_Process() {
    g_game_process_hook.call<void>();

    if (g_drone_active && g_drone) {
        float dt = 1.0f / 60.0f;
        if (g_re3.ms_fTimeStep && *g_re3.ms_fTimeStep > 0.0f) {
            dt = *g_re3.ms_fTimeStep / 50.0f;
        }
        if (dt > 0.1f) dt = 0.1f;

        g_last_frame.struct_size = sizeof(fpv_frame);
        if (fpv_drone_tick(g_drone, dt, &g_last_frame) == 0) {
            g_has_frame = true;
            CVector drone_pos{
                g_last_frame.position[0],
                g_last_frame.position[1],
                g_last_frame.position[2]
            };
            update_drone_streaming(drone_pos, dt);
        }
    }
}

void hooked_CCamera_Process(void* self) {
    g_camera_process_hook.call<void>(self);
    apply_drone_to_camera();
}

void hooked_CPlayerPed_ProcessControl(void* self) {
    if (g_drone_active || fpv_overlay_is_open() != 0) {
        return;
    }
    g_player_control_hook.call<void>(self);
}

const CVector& hooked_FindPlayerCentreOfWorld(int player_id) {
    static CVector drone_pos{};
    if (g_drone_active && g_has_frame) {
        drone_pos = CVector{
            g_last_frame.position[0],
            g_last_frame.position[1],
            g_last_frame.position[2]
        };
        return drone_pos;
    }
    return g_player_centre_hook.original<const CVector& (*)(int)>()(player_id);
}

__attribute__((constructor))
void on_load() {
    std::fprintf(stderr, "[re3-fpv] Initializing...\n");

    if (!g_re3.load()) {
        std::fprintf(stderr, "[re3-fpv] ERROR: failed to resolve one or more re3 symbols.\n");
        return;
    }

    g_swap_hook = safetyhook::create_inline(
        reinterpret_cast<void*>(g_re3.glfwSwapBuffers),
        reinterpret_cast<void*>(&hooked_glfwSwapBuffers)
    );
    g_game_process_hook = safetyhook::create_inline(
        reinterpret_cast<void*>(g_re3.CGame_Process),
        reinterpret_cast<void*>(&hooked_CGame_Process)
    );
    g_camera_process_hook = safetyhook::create_inline(
        reinterpret_cast<void*>(g_re3.CCamera_Process),
        reinterpret_cast<void*>(&hooked_CCamera_Process)
    );
    g_player_control_hook = safetyhook::create_inline(
        reinterpret_cast<void*>(g_re3.CPlayerPed_ProcessControl),
        reinterpret_cast<void*>(&hooked_CPlayerPed_ProcessControl)
    );
    g_player_centre_hook = safetyhook::create_inline(
        reinterpret_cast<void*>(g_re3.FindPlayerCentreOfWorld),
        reinterpret_cast<void*>(&hooked_FindPlayerCentreOfWorld)
    );

    fpv_global_config cfg{};
    fpv_global_config_defaults(&cfg);
    cfg.data_dir = "./fpv_data";
    cfg.coords = fpv_coords_preset(FPV_COORDS_Z_UP_RIGHT_HANDED, 1.0f);

    if (fpv_global_init(&cfg) == 0) {
        fpv_world_set_raycast_callback(&re3_raycast_cb, nullptr);
        g_fpv_ready = true;
        std::fprintf(stderr, "[re3-fpv] Ready. F7: spawn/recall | F8: overlay | Enter: arm | WASD+Arrows: fly | R: respawn\n");
    } else {
        std::fprintf(stderr, "[re3-fpv] ERROR: fpv_global_init failed.\n");
    }
}

__attribute__((destructor))
void on_unload() {
    set_drone_streaming_enabled(false);
    if (g_gl3_ready) {
        fpv_gl3_shutdown(&g_gl3);
        g_gl3_ready = false;
    }
    if (g_drone) {
        fpv_drone_delete(g_drone);
        g_drone = nullptr;
    }
    if (g_fpv_ready) {
        fpv_global_terminate();
        g_fpv_ready = false;
    }
}

} // namespace
