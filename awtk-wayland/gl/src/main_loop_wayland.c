/*
 * main_loop_wayland.1.c
 *
 *  Created on: 2019��11��18��
 *      Author: zjm09
 */
#include <poll.h>
#include "GLES2/gl2.h"
#include "GLES2/gl2ext.h"
#include "EGL/egl.h"
#include "main_loop/main_loop_simple.h"
#ifdef WITH_NANOVG_GL
#include "native_window/native_window_fb_gl.h"
#else
#include "native_window/native_window_raw.h"
#endif
#include "tkc/thread.h"

#include "lcd_wayland.h"

static EGLint               numconfigs;
static EGLDisplay           egldisplay;
static EGLConfig            EglConfig;
static EGLSurface           eglsurface;
static EGLContext           EglContext;
static EGLNativeWindowType  eglNativeWindow;
static EGLNativeDisplayType eglNativeDisplayType;
static lcd_wayland_t *lw;

static void *wayland_run(void* ctx);

static ret_t main_loop_linux_destroy(main_loop_t* l) {
  main_loop_simple_t* loop = (main_loop_simple_t*)l;

  main_loop_simple_reset(loop);

  return RET_OK;
}

void Init_GLES(lcd_wayland_t *lw)
{
	static const EGLint ContextAttributes[] = {
			EGL_CONTEXT_CLIENT_VERSION, 2,
			EGL_NONE
		};
  static const EGLint s_configAttribs[] =
  {
		  EGL_RED_SIZE,     1,
		  EGL_GREEN_SIZE,   1,
		  EGL_BLUE_SIZE,    1,
		  EGL_ALPHA_SIZE,   1,
		  EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		  EGL_NONE,
  };

//  lcd_wayland_t *lw = lcd->impl_data;


  eglNativeDisplayType = (EGLNativeDisplayType)lw->objs.display;	// wl_display *
  egldisplay = eglGetDisplay(eglNativeDisplayType);
  eglInitialize(egldisplay, NULL, NULL);
  assert(eglGetError() == EGL_SUCCESS);
  eglBindAPI(EGL_OPENGL_ES_API);

  EGLint ConfigCount;
  EGLint ConfigNumberOfFrameBufferConfigurations;
  EGLint ConfigValue;

  eglGetConfigs(egldisplay, 0, 0, &ConfigCount);
  EGLConfig *EglAllConfigs = calloc(ConfigCount, sizeof(*EglAllConfigs));
  eglChooseConfig(egldisplay, s_configAttribs, EglAllConfigs, ConfigCount, &ConfigNumberOfFrameBufferConfigurations);
  for (int i = 0; i < ConfigNumberOfFrameBufferConfigurations; ++i)
  {
  		eglGetConfigAttrib(egldisplay, EglAllConfigs[i], EGL_BUFFER_SIZE, &ConfigValue);
  		if (ConfigValue == 32) // NOTE(Felix): Magic value from weston example
  		{
  			EglConfig = EglAllConfigs[i];
  			break;
  		}
  }
  free(EglAllConfigs);
  EglContext = eglCreateContext(egldisplay, EglConfig, EGL_NO_CONTEXT, ContextAttributes);

  eglNativeWindow = (EGLNativeWindowType)wl_egl_window_create(lw->objs.surface, lw->objs.width, lw->objs.height);
  assert(eglNativeWindow);

  eglsurface = eglCreateWindowSurface(egldisplay, EglConfig, eglNativeWindow, NULL);
  assert(eglGetError() == EGL_SUCCESS);

//  struct xdg_surface *XdgSurface = xdg_wm_base_get_xdg_surface(Window.XdgBase, eglsurface);

  eglMakeCurrent(egldisplay, eglsurface, eglsurface, EglContext);
  assert(eglGetError() == EGL_SUCCESS);

  glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);

  eglSwapInterval(egldisplay, 60);
  assert(eglGetError() == EGL_SUCCESS);

}

static ret_t gles_swap_buffer(native_window_t* win) {

//	uint64_t t = time_now_ms();
//	static uint64_t old_t;
//
//	t = t - old_t;
//	old_t = t;
//
//	printf("fps:%0.2f\n",1000/(t/1000.0));

	//wl_surface_set_opaque_region(lw->objs.surface, NULL);

  eglSwapBuffers(egldisplay, eglsurface);
  return RET_OK;
}

static ret_t gles_make_current(native_window_t* win) {

  EGLint width = 0;
  EGLint height = 0;
  eglQuerySurface(egldisplay, eglsurface, EGL_WIDTH, &width);
  eglQuerySurface(egldisplay, eglsurface, EGL_HEIGHT, &height);

  eglMakeCurrent(egldisplay, eglsurface, eglsurface, EglContext);
  assert(eglGetError() == EGL_SUCCESS);

  glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  glViewport(0, 0, width, height);
  glClearColor(1.0f, 1.0f, 1.0f, 1.0f );

  return RET_OK;
}

main_loop_t* main_loop_init(int w, int h) {
	lw = lcd_wayland_create();
//	int w,h;

	return_value_if_fail(lw != NULL, NULL);
#ifdef WITH_NANOVG_GL
	Init_GLES(lw);

	eglQuerySurface(egldisplay, eglsurface, EGL_WIDTH, &w);
	eglQuerySurface(egldisplay, eglsurface, EGL_HEIGHT, &h);

	native_window_t* win = native_window_fb_gl_init(w, h,1.0);

	native_window_fb_gl_set_swap_buffer_func(win, gles_swap_buffer);
	native_window_fb_gl_set_make_current_func(win, gles_make_current);
#else
	native_window_raw_init(lcd);
#endif
	main_loop_simple_t *loop = main_loop_simple_init(w, h, NULL, NULL);

	loop->base.destroy = main_loop_linux_destroy;
//	canvas_init(&(loop->base.canvas), lcd, font_manager());

	tk_thread_t* thread = tk_thread_create(wayland_run, lw);
	if (thread != NULL) {
		tk_thread_start(thread);
	}

	return (main_loop_t*)loop;
}

static void PlatformPollEvents(struct wayland_data *objs)
{
    struct wl_display* display = objs->display;
    struct pollfd fds[] = {
        { wl_display_get_fd(display), POLLIN },
    };

    while (wl_display_prepare_read(display) != 0)
        wl_display_dispatch_pending(display);
    wl_display_flush(display);
    if (poll(fds, 1, 50) > 0)
    {
        wl_display_read_events(display);
        wl_display_dispatch_pending(display);
    }
    else
    {
        wl_display_cancel_read(display);
    }
}

static void* wayland_run(void* ctx)
{
	lcd_wayland_t* lw = (lcd_wayland_t *)ctx;

	while(1){
		PlatformPollEvents(&lw->objs);
	}
}

