// Ponto de entrada Android (NativeActivity + android_native_app_glue, sem Java).
#include <android/input.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

#include <ctime>
#include <memory>

#include "core/log.h"
#include "game/game.h"

namespace {

double nowSeconds() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}

struct AppState {
    Renderer renderer;
    std::unique_ptr<Game> game;
    bool instanceReady = false;
    bool hasWindow = false;
    bool focused = false;
    bool canRender() const { return hasWindow && focused && game; }
};

void createSurface(android_app* app, AppState& s) {
    if (!s.instanceReady) {
        if (!s.renderer.createInstance({VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME}, false)) {
            LOGE("Falha ao criar instância Vulkan (aparelho sem Vulkan?)");
            return;
        }
        s.instanceReady = true;
    }
    VkAndroidSurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
    sci.window = app->window;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (vkCreateAndroidSurfaceKHR(s.renderer.instance(), &sci, nullptr, &surface) != VK_SUCCESS) {
        LOGE("vkCreateAndroidSurfaceKHR falhou");
        return;
    }
    uint32_t w = static_cast<uint32_t>(ANativeWindow_getWidth(app->window));
    uint32_t h = static_cast<uint32_t>(ANativeWindow_getHeight(app->window));
    if (!s.renderer.attachSurface(surface, w, h)) {
        LOGE("attachSurface falhou");
        return;
    }
    if (!s.game) {
        GameConfig cfg;
        cfg.touchUi = true;
        cfg.renderRadius = 8;
        s.game = std::make_unique<Game>(s.renderer, cfg);
    }
    s.game->onRendererReady();
    s.hasWindow = true;
}

void onAppCmd(android_app* app, int32_t cmd) {
    auto& s = *static_cast<AppState*>(app->userData);
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window) createSurface(app, s);
            break;
        case APP_CMD_TERM_WINDOW:
            s.hasWindow = false;
            s.renderer.detachSurface();
            if (s.game) s.game->touch().onCancelAll();
            break;
        case APP_CMD_GAINED_FOCUS: s.focused = true; break;
        case APP_CMD_LOST_FOCUS:
            s.focused = false;
            if (s.game) s.game->touch().onCancelAll();
            break;
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONFIG_CHANGED:
        case APP_CMD_CONTENT_RECT_CHANGED:
            if (app->window && s.hasWindow)
                s.renderer.notifyResize(static_cast<uint32_t>(ANativeWindow_getWidth(app->window)),
                                        static_cast<uint32_t>(ANativeWindow_getHeight(app->window)));
            break;
        default: break;
    }
}

int32_t onInputEvent(android_app* app, AInputEvent* event) {
    auto& s = *static_cast<AppState*>(app->userData);
    if (!s.game || AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) return 0;
    TouchControls& tc = s.game->touch();
    int32_t action = AMotionEvent_getAction(event);
    int32_t masked = action & AMOTION_EVENT_ACTION_MASK;
    size_t index = static_cast<size_t>((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                                       AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
    double t = nowSeconds();
    switch (masked) {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN:
            tc.onDown(AMotionEvent_getPointerId(event, index), AMotionEvent_getX(event, index),
                      AMotionEvent_getY(event, index), t);
            break;
        case AMOTION_EVENT_ACTION_MOVE: {
            size_t count = AMotionEvent_getPointerCount(event);
            for (size_t i = 0; i < count; ++i)
                tc.onMove(AMotionEvent_getPointerId(event, i), AMotionEvent_getX(event, i), AMotionEvent_getY(event, i));
            break;
        }
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP:
            tc.onUp(AMotionEvent_getPointerId(event, index), t);
            break;
        case AMOTION_EVENT_ACTION_CANCEL: tc.onCancelAll(); break;
        default: break;
    }
    return 1;
}

}  // namespace

void android_main(android_app* app) {
    AppState state;
    app->userData = &state;
    app->onAppCmd = onAppCmd;
    app->onInputEvent = onInputEvent;

    while (!app->destroyRequested) {
        // Sem janela/foco: bloqueia esperando eventos (não gasta bateria).
        for (;;) {
            int events = 0;
            android_poll_source* source = nullptr;
            int ident = ALooper_pollOnce(state.canRender() ? 0 : -1, nullptr, &events, reinterpret_cast<void**>(&source));
            if (ident < 0 && ident != ALOOPER_POLL_CALLBACK) break;  // timeout / erro
            if (source) source->process(app, source);
            if (app->destroyRequested) break;
        }
        if (app->destroyRequested) break;
        if (state.canRender()) state.game->frame(nowSeconds(), FrameInput{});
    }
    state.game.reset();
    state.renderer.shutdown();
}
