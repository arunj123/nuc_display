#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <poll.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <math.h>
#include <signal.h>

#define EXIT_ERROR(...) do { \
    fprintf(stderr, "ERROR: " __VA_ARGS__); \
    fprintf(stderr, "\n"); \
    exit(1); \
} while(0)

volatile sig_atomic_t g_running = 1;

static void sigint_handler(int signum) {
    g_running = 0;
}

struct drm_system {
    int fd;
    drmModeRes *resources;
    drmModeConnector *connector;
    drmModeEncoder *encoder;
    drmModeModeInfo mode;
    uint32_t crtc_id;
};

struct gbm_system {
    struct gbm_device *dev;
    struct gbm_surface *surface;
};

struct egl_system {
    EGLDisplay display;
    EGLConfig config;
    EGLContext context;
    EGLSurface surface;
};

static void init_drm(struct drm_system *drm) {
    for (int i = 0; i < 10; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/dri/card%d", i);
        drm->fd = open(path, O_RDWR | O_CLOEXEC);
        if (drm->fd >= 0) {
            drm->resources = drmModeGetResources(drm->fd);
            if (drm->resources && drm->resources->count_connectors > 0) {
                printf("Successfully opened %s\n", path);
                break;
            }
            if (drm->resources) {
                drmModeFreeResources(drm->resources);
                drm->resources = NULL;
            }
            close(drm->fd);
            drm->fd = -1;
        }
    }

    if (drm->fd < 0 || !drm->resources) {
        EXIT_ERROR("Could not open any /dev/dri/cardX with KMS resources. Check permissions or check if Intel drivers are loaded.");
    }

    // Wait for a connected connector (Hotplug handling)
    bool connected = false;
    while (!connected) {
        for (int i = 0; i < drm->resources->count_connectors; i++) {
            drmModeConnector *conn = drmModeGetConnector(drm->fd, drm->resources->connectors[i]);
            if (!conn) continue;

            if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0) {
                drm->connector = conn;
                drm->mode = conn->modes[0]; // Pick highest resolution mode
                connected = true;
                break;
            }
            drmModeFreeConnector(conn);
        }
        
        if (!connected) {
            printf("No display connected yet. Waiting for HDMI/DP hotplug...\n");
            sleep(2);
            // Refresh resources
            drmModeFreeResources(drm->resources);
            drm->resources = drmModeGetResources(drm->fd);
        }
    }

    printf("Found Display! Connected to: %dx%d\n", drm->mode.hdisplay, drm->mode.vdisplay);

    // Find encoder
    drm->encoder = drmModeGetEncoder(drm->fd, drm->connector->encoders[0]);
    if (!drm->encoder) {
        EXIT_ERROR("Could not find suitable DRM encoder");
    }

    drm->crtc_id = drm->encoder->crtc_id;
    if (!drm->crtc_id) {
        // Fallback: search for CRTC manually if encoder doesn't have one active
        for (int i = 0; i < drm->resources->count_crtcs; i++) {
            if (drm->encoder->possible_crtcs & (1 << i)) {
                drm->crtc_id = drm->resources->crtcs[i];
                break;
            }
        }
    }
    if (!drm->crtc_id) {
        EXIT_ERROR("Could not find suitable CRTC");
    }
}

static void init_gbm(struct drm_system *drm, struct gbm_system *gbm) {
    gbm->dev = gbm_create_device(drm->fd);
    if (!gbm->dev) {
        EXIT_ERROR("Failed to create GBM device");
    }

    gbm->surface = gbm_surface_create(gbm->dev,
                                      drm->mode.hdisplay,
                                      drm->mode.vdisplay,
                                      GBM_FORMAT_XRGB8888,
                                      GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!gbm->surface) {
        EXIT_ERROR("Failed to create GBM surface");
    }
}

static void init_egl(struct gbm_system *gbm, struct egl_system *egl) {
    EGLint major, minor;
    egl->display = eglGetDisplay((EGLNativeDisplayType)gbm->dev);
    if (egl->display == EGL_NO_DISPLAY) {
        EXIT_ERROR("Failed to get EGL display");
    }

    if (!eglInitialize(egl->display, &major, &minor)) {
        EXIT_ERROR("Failed to initialize EGL");
    }

    eglBindAPI(EGL_OPENGL_ES_API);

    // Simply let EGL select a config that matches the GBM surface.
    // 0x3009 is EGL_BAD_MATCH, often caused by over-specifying attributes that the GBM surface does not perfectly support.
    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    EGLint num_configs;
    // We get all matching configs and find the one with the correct native visual ID explicitly
    EGLConfig *configs = NULL;
    eglChooseConfig(egl->display, config_attribs, NULL, 0, &num_configs);
    
    if (num_configs == 0) EXIT_ERROR("No EGL configs found");
    
    configs = malloc(num_configs * sizeof(EGLConfig));
    eglChooseConfig(egl->display, config_attribs, configs, num_configs, &num_configs);
    
    bool found_config = false;
    for (int i = 0; i < num_configs; i++) {
        EGLint visual_id;
        if (eglGetConfigAttrib(egl->display, configs[i], EGL_NATIVE_VISUAL_ID, &visual_id)) {
            if (visual_id == GBM_FORMAT_XRGB8888) {
                egl->config = configs[i];
                found_config = true;
                break;
            }
        }
    }
    
    free(configs);

    if (!found_config) {
        printf("Failed to find EGL config that explicitly matches GBM_FORMAT_XRGB8888. Falling back to first available.\n");
        // Fallback to choosing the first generic one that might work if strict id matching fails
        EGLint fallback_attribs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_NONE
        };
        if (!eglChooseConfig(egl->display, fallback_attribs, &egl->config, 1, &num_configs) || num_configs == 0) {
            EXIT_ERROR("Failed to choose any EGL config");
        }
    }

    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    egl->context = eglCreateContext(egl->display, egl->config, EGL_NO_CONTEXT, context_attribs);
    if (egl->context == EGL_NO_CONTEXT) {
        printf("eglCreateContext Error: %x\n", eglGetError());
        EXIT_ERROR("Failed to create EGL context");
    }

    egl->surface = eglCreateWindowSurface(egl->display, egl->config, (EGLNativeWindowType)gbm->surface, NULL);
    if (egl->surface == EGL_NO_SURFACE) {
        printf("eglCreateWindowSurface Error: %x\n", eglGetError());
        EXIT_ERROR("Failed to create EGL surface");
    }

    eglMakeCurrent(egl->display, egl->surface, egl->surface, egl->context);
}

// Global state for page flip handler
struct gbm_bo *previous_bo = NULL;
uint32_t previous_fb;
bool waiting_for_flip = false;

static void page_flip_handler(int fd, unsigned int frame,
                              unsigned int sec, unsigned int usec, void *data) {
    waiting_for_flip = false;
    
    // Cleanup previous BO now that it isn't on screen anymore
    if (previous_bo) {
        drmModeRmFB(fd, previous_fb);
        gbm_surface_release_buffer((struct gbm_surface *)data, previous_bo);
    }
}

int main(int argc, char **argv) {
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    printf("Initializing Native Displays...\n");

    struct drm_system drm = {0};
    struct gbm_system gbm = {0};
    struct egl_system egl = {0};

    init_drm(&drm);
    init_gbm(&drm, &gbm);
    init_egl(&gbm, &egl);

    printf("Starting render loop...\n");

    drmEventContext evctx = {
        .version = DRM_EVENT_CONTEXT_VERSION,
        .page_flip_handler = page_flip_handler,
    };

    // To handle page flip
    struct pollfd pfd = { .fd = drm.fd, .events = POLLIN };

    float color_offset = 0.0f;

    int ret = 0;
    while (g_running) {
        // --- RENDER GL FRAME ---
        float r = 0.5f + 0.5f * sinf(color_offset);
        float g = 0.5f + 0.5f * sinf(color_offset + 2.094f);
        float b = 0.5f + 0.5f * sinf(color_offset + 4.188f);
        color_offset += 0.01f;

        glClearColor(r, g, b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // --- SWAP BUFFERS ---
        eglSwapBuffers(egl.display, egl.surface);

        // --- FETCH NEW BO & PAGE_FLIP ---
        struct gbm_bo *bo = gbm_surface_lock_front_buffer(gbm.surface);
        if (!bo) {
            fprintf(stderr, "Failed to lock front buffer\n");
            ret = 1;
            break;
        }
        uint32_t handle = gbm_bo_get_handle(bo).u32;
        uint32_t pitch = gbm_bo_get_stride(bo);
        
        uint32_t fb;
        if (drmModeAddFB(drm.fd, drm.mode.hdisplay, drm.mode.vdisplay, 24, 32, pitch, handle, &fb)) {
            fprintf(stderr, "Failed to create DRM Framebuffer\n");
            ret = 1;
            break;
        }

        if (!previous_bo) {
            // First frame: set CRTC
            if (drmModeSetCrtc(drm.fd, drm.crtc_id, fb, 0, 0, &drm.connector->connector_id, 1, &drm.mode)) {
                fprintf(stderr, "Failed to set CRTC (Did you unplug?)\n");
                ret = 1;
                break;
            }
        } else {
            // Successive frames: Page flip safely
            if (drmModePageFlip(drm.fd, drm.crtc_id, fb, DRM_MODE_PAGE_FLIP_EVENT, gbm.surface)) {
                fprintf(stderr, "Warning: Page flip failed (Display disconnected?). Retrying initialization...\n");
                ret = 1; 
                break;
            }
            waiting_for_flip = true;
            
            // Wait for flip to complete (VSync)
            while (waiting_for_flip && g_running) {
                if (poll(&pfd, 1, 100) > 0) {
                    drmHandleEvent(drm.fd, &evctx);
                }
            }
        }

        previous_bo = bo;
        previous_fb = fb;
    }

    // --- GRACEFUL SHUTDOWN ---
    printf("\nShutting down gracefully...\n");

    // Clear the screen by setting the CRTC back to nothing or its original state
    // We can simply disable the CRTC to turn the screen off (or back to console)
    drmModeSetCrtc(drm.fd, drm.crtc_id, 0, 0, 0, NULL, 0, NULL);

    // Clean up EGL
    eglMakeCurrent(egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(egl.display, egl.surface);
    eglDestroyContext(egl.display, egl.context);
    eglTerminate(egl.display);

    // Clean up GBM
    if (previous_bo) {
        drmModeRmFB(drm.fd, previous_fb);
        gbm_surface_release_buffer(gbm.surface, previous_bo);
    }
    gbm_surface_destroy(gbm.surface);
    gbm_device_destroy(gbm.dev);

    // Clean up DRM
    drmModeFreeEncoder(drm.encoder);
    drmModeFreeConnector(drm.connector);
    if (drm.resources) {
        drmModeFreeResources(drm.resources);
    }
    close(drm.fd);

    printf("Cleanup complete. Exiting.\n");
    return ret;
}
