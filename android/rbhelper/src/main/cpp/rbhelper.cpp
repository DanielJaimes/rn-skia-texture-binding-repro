// Allocates an RGBA8 AHardwareBuffer and fills it on the GPU (or the CPU) so JS can import it with
// Skia.Image.MakeImageFromNativeBuffer. Returns the pointer as {hi, lo} 32-bit halves.
#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <android/hardware_buffer.h>
#include <jni.h>
#include <cstring>

extern "C" JNIEXPORT jlongArray JNICALL
Java_com_skiarepro_RBHelperModule_nativeMake(JNIEnv *env, jclass, jboolean cpu, jint w, jint h) {
  AHardwareBuffer_Desc d = {};
  d.width = w; d.height = h; d.layers = 1; d.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
  d.usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT |
            (cpu ? AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN : 0);
  AHardwareBuffer *buf = nullptr;
  if (AHardwareBuffer_allocate(&d, &buf) != 0) return nullptr;
  if (cpu) {  // CPU-written variant: same usage flags plus CPU_WRITE
    void *p = nullptr; AHardwareBuffer_Desc a; AHardwareBuffer_describe(buf, &a);
    AHardwareBuffer_lock(buf, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, nullptr, &p);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
      uint8_t *px = (uint8_t *)p + (y * a.stride + x) * 4;
      px[0] = x * 255 / w; px[1] = y * 255 / h; px[2] = ((x + y) & 32) ? 220 : 30; px[3] = 255;
    }
    AHardwareBuffer_unlock(buf, nullptr);
  } else {  // GPU-written: EGLImage + FBO on a private context, scissored clears
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY); eglInitialize(dpy, nullptr, nullptr);
    EGLint ca[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE};
    EGLConfig cfg; EGLint n; eglChooseConfig(dpy, ca, &cfg, 1, &n);
    EGLint xa[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE}, pa[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, xa);
    EGLSurface sf = eglCreatePbufferSurface(dpy, cfg, pa); eglMakeCurrent(dpy, sf, sf, ctx);
    EGLint ia[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
    EGLImageKHR img = eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID,
                                        eglGetNativeClientBufferANDROID(buf), ia);
    GLuint tex, fbo; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    ((PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES"))(GL_TEXTURE_2D, img);
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glEnable(GL_SCISSOR_TEST);
    for (int i = 0; i < 16; i++) {  // 16 horizontal bands, cyan..yellow gradient
      glScissor(0, i * h / 16, w, h / 16); glClearColor(i / 15.f, 1.f, 1.f - i / 15.f, 1.f); glClear(GL_COLOR_BUFFER_BIT);
    }
    glScissor(w / 4, h / 4, w / 2, h / 2); glClearColor(1.f, 0.f, 1.f, 1.f); glClear(GL_COLOR_BUFFER_BIT);  // magenta centre
    glFinish();
    glDeleteFramebuffers(1, &fbo); glDeleteTextures(1, &tex); eglDestroyImageKHR(dpy, img);
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT); eglDestroySurface(dpy, sf); eglDestroyContext(dpy, ctx);
  }
  jlong v[2] = {(jlong)((uintptr_t)buf >> 32), (jlong)((uintptr_t)buf & 0xffffffffu)};
  jlongArray r = env->NewLongArray(2); env->SetLongArrayRegion(r, 0, 2, v); return r;
}
