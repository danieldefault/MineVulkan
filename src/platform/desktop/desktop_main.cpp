// Versão desktop (Linux, XCB) — para iterar rápido no PC e testar sem celular.
// Funciona em X11 e em Wayland via XWayland.
// Controles: WASD mover, espaço pular, mouse olhar, botão esq. quebrar, dir. colocar,
// 1-9, 0/roda selecionar bloco, Esc libera o mouse, clique captura de novo.
#include <xcb/xcb.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_xcb.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/log.h"
#include "game/game.h"

namespace {

double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// Keycodes X11 padrão (evdev + 8), válidos em qualquer layout físico QWERTY.
enum : uint8_t { KC_ESC = 9, KC_1 = 10, KC_W = 25, KC_A = 38, KC_S = 39, KC_D = 40, KC_SPACE = 65 };

struct Keys {
    bool w = false, a = false, s = false, d = false, space = false;
};

xcb_atom_t internAtom(xcb_connection_t* c, const char* name) {
    xcb_intern_atom_reply_t* r =
        xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, static_cast<uint16_t>(std::strlen(name)), name), nullptr);
    xcb_atom_t a = r ? r->atom : XCB_ATOM_NONE;
    std::free(r);
    return a;
}

}  // namespace

int main(int argc, char** argv) {
    int maxFrames = -1;      // --frames N: sai após N frames (testes automatizados)
    float turnPerFrame = 0;  // --turn R: gira a câmera R rad/frame
    bool walk = false;       // --walk: anda para frente sozinho
    GameConfig cfg;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--frames" && i + 1 < argc) maxFrames = std::atoi(argv[++i]);
        else if (a == "--radius" && i + 1 < argc) cfg.renderRadius = std::atoi(argv[++i]);
        else if (a == "--seed" && i + 1 < argc) cfg.seed = static_cast<uint32_t>(std::atoi(argv[++i]));
        else if (a == "--turn" && i + 1 < argc) turnPerFrame = static_cast<float>(std::atof(argv[++i]));
        else if (a == "--walk") walk = true;
        else if (a == "--touch-ui") cfg.touchUi = true;
        else if (a == "--look" && i + 2 < argc) {
            cfg.spawnYaw = static_cast<float>(std::atof(argv[++i]));
            cfg.spawnPitch = static_cast<float>(std::atof(argv[++i]));
        } else if (a == "--spawn" && i + 2 < argc) {
            cfg.spawnX = std::atoi(argv[++i]);
            cfg.spawnZ = std::atoi(argv[++i]);
        }
    }
    const char* validationEnv = std::getenv("VC_VALIDATION");
    bool validation = validationEnv && std::strcmp(validationEnv, "0") != 0;

    int screenNum = 0;
    xcb_connection_t* conn = xcb_connect(nullptr, &screenNum);
    if (xcb_connection_has_error(conn)) {
        LOGE("Não foi possível conectar ao servidor X (DISPLAY)");
        return 1;
    }
    xcb_screen_iterator_t it = xcb_setup_roots_iterator(xcb_get_setup(conn));
    for (int i = 0; i < screenNum; ++i) xcb_screen_next(&it);
    xcb_screen_t* screen = it.data;

    uint16_t width = 1280, height = 720;
    xcb_window_t win = xcb_generate_id(conn);
    uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    uint32_t values[2] = {screen->black_pixel,
                          XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS |
                              XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_STRUCTURE_NOTIFY |
                              XCB_EVENT_MASK_FOCUS_CHANGE};
    xcb_create_window(conn, XCB_COPY_FROM_PARENT, win, screen->root, 0, 0, width, height, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, mask, values);
    const char* title = "VoxelCraft";
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
                        static_cast<uint32_t>(std::strlen(title)), title);
    xcb_atom_t wmProtocols = internAtom(conn, "WM_PROTOCOLS");
    xcb_atom_t wmDelete = internAtom(conn, "WM_DELETE_WINDOW");
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, wmProtocols, XCB_ATOM_ATOM, 32, 1, &wmDelete);
    xcb_map_window(conn, win);

    // Cursor invisível para o modo "mouse capturado".
    xcb_pixmap_t pix = xcb_generate_id(conn);
    xcb_create_pixmap(conn, 1, pix, win, 1, 1);
    xcb_cursor_t invisible = xcb_generate_id(conn);
    xcb_create_cursor(conn, invisible, pix, pix, 0, 0, 0, 0, 0, 0, 0, 0);
    xcb_free_pixmap(conn, pix);
    xcb_flush(conn);

    Renderer renderer;
    if (!renderer.createInstance({VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XCB_SURFACE_EXTENSION_NAME}, validation))
        return 1;
    VkXcbSurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR};
    sci.connection = conn;
    sci.window = win;
    VkSurfaceKHR surface;
    if (vkCreateXcbSurfaceKHR(renderer.instance(), &sci, nullptr, &surface) != VK_SUCCESS) {
        LOGE("vkCreateXcbSurfaceKHR falhou");
        return 1;
    }
    if (!renderer.attachSurface(surface, width, height)) return 1;

    Game game(renderer, cfg);
    game.onRendererReady();

    Keys keys;
    bool grabbed = false;
    bool running = true;
    int frames = 0;
    auto warpCenter = [&]() {
        xcb_warp_pointer(conn, XCB_NONE, win, 0, 0, 0, 0, static_cast<int16_t>(width / 2), static_cast<int16_t>(height / 2));
        xcb_flush(conn);
    };
    auto setGrab = [&](bool g) {
        grabbed = g;
        if (g) {
            xcb_grab_pointer(conn, 1, win, XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_PRESS,
                             XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, win, invisible, XCB_CURRENT_TIME);
            warpCenter();
        } else {
            xcb_ungrab_pointer(conn, XCB_CURRENT_TIME);
            xcb_flush(conn);
        }
    };

    while (running) {
        FrameInput in;
        while (xcb_generic_event_t* ev = xcb_poll_for_event(conn)) {
            switch (ev->response_type & 0x7F) {
                case XCB_CLIENT_MESSAGE: {
                    auto* cm = reinterpret_cast<xcb_client_message_event_t*>(ev);
                    if (cm->data.data32[0] == wmDelete) running = false;
                    break;
                }
                case XCB_CONFIGURE_NOTIFY: {
                    auto* ce = reinterpret_cast<xcb_configure_notify_event_t*>(ev);
                    if (ce->width != width || ce->height != height) {
                        width = ce->width;
                        height = ce->height;
                        renderer.notifyResize(width, height);
                    }
                    break;
                }
                case XCB_KEY_PRESS:
                case XCB_KEY_RELEASE: {
                    bool down = (ev->response_type & 0x7F) == XCB_KEY_PRESS;
                    uint8_t kc = reinterpret_cast<xcb_key_press_event_t*>(ev)->detail;
                    if (kc == KC_W) keys.w = down;
                    else if (kc == KC_A) keys.a = down;
                    else if (kc == KC_S) keys.s = down;
                    else if (kc == KC_D) keys.d = down;
                    else if (kc == KC_SPACE) keys.space = down;
                    else if (kc == KC_ESC && down) setGrab(false);
                    else if (down && kc >= KC_1 && kc < KC_1 + kHotbarSize) in.selectSlot = kc - KC_1;  // 1..9, 0
                    break;
                }
                case XCB_BUTTON_PRESS: {
                    uint8_t b = reinterpret_cast<xcb_button_press_event_t*>(ev)->detail;
                    if (!grabbed) { setGrab(true); break; }
                    if (b == 1) in.breakBlock = true;
                    else if (b == 3) in.placeBlock = true;
                    else if (b == 4) in.slotDelta -= 1;
                    else if (b == 5) in.slotDelta += 1;
                    break;
                }
                case XCB_MOTION_NOTIFY:
                    if (grabbed) {
                        auto* me = reinterpret_cast<xcb_motion_notify_event_t*>(ev);
                        int dx = me->event_x - width / 2, dy = me->event_y - height / 2;
                        if (dx || dy) {
                            in.player.lookYaw += static_cast<float>(dx) * 0.0025f;
                            in.player.lookPitch -= static_cast<float>(dy) * 0.0025f;
                            warpCenter();
                        }
                    }
                    break;
                case XCB_FOCUS_OUT: keys = Keys{}; break;
                default: break;
            }
            std::free(ev);
        }
        if (xcb_connection_has_error(conn)) running = false;

        in.player.moveZ = (keys.w ? 1.0f : 0.0f) - (keys.s ? 1.0f : 0.0f);
        in.player.moveX = (keys.d ? 1.0f : 0.0f) - (keys.a ? 1.0f : 0.0f);
        in.player.jump = keys.space;
        if (walk) in.player.moveZ = 1.0f;
        in.player.lookYaw += turnPerFrame;

        game.frame(nowSeconds(), in);
        if (maxFrames > 0 && ++frames >= maxFrames) running = false;
    }

    uint32_t errors = renderer.validationErrors();
    renderer.shutdown();
    xcb_free_cursor(conn, invisible);
    xcb_destroy_window(conn, win);
    xcb_disconnect(conn);
    if (errors) {
        LOGE("%u erro(s) de validação", errors);
        return 2;
    }
    return 0;
}
