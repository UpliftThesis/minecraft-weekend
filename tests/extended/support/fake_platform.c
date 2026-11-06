/* See fake_platform.h. Also provides the global `state` normally defined in
 * main.c (which is excluded from the headless build) and no-op GLFW. */
#include <string.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "state.h"
#include "fake_platform.h"

struct State state;
struct FakeGLStats fake_gl;

static GLuint next_name = 1;

void fake_gl_reset_stats(void) { memset(&fake_gl, 0, sizeof fake_gl); }

/* ---- recording fakes ---- */
static void APIENTRY f_DrawElements(GLenum m, GLsizei count, GLenum t, const void *i) {
    (void)m; (void)t; (void)i;
    fake_gl.draw_calls++;
    if (count > 0) fake_gl.drawn_indices += (size_t)count;
}
static void APIENTRY f_BufferData(GLenum target, GLsizeiptr size, const void *d, GLenum u) {
    (void)d; (void)u;
    if (target == GL_ELEMENT_ARRAY_BUFFER) {
        fake_gl.element_uploads++; fake_gl.element_bytes += (size_t)size; fake_gl.last_element_bytes = (size_t)size;
    } else {
        fake_gl.array_uploads++; fake_gl.array_bytes += (size_t)size; fake_gl.last_array_bytes = (size_t)size;
    }
}
static void gen(GLsizei n, GLuint *out, long *live) { for (GLsizei i = 0; i < n; i++) out[i] = next_name++; *live += n; }
static void APIENTRY f_GenBuffers(GLsizei n, GLuint *o) { gen(n, o, &fake_gl.live_buffers); }
static void APIENTRY f_GenVertexArrays(GLsizei n, GLuint *o) { gen(n, o, &fake_gl.live_vertex_arrays); }
static void APIENTRY f_GenTextures(GLsizei n, GLuint *o) { gen(n, o, &fake_gl.live_textures); }
static void APIENTRY f_DeleteBuffers(GLsizei n, const GLuint *o) { (void)o; fake_gl.live_buffers -= n; }
static void APIENTRY f_DeleteVertexArrays(GLsizei n, const GLuint *o) { (void)o; fake_gl.live_vertex_arrays -= n; }
static void APIENTRY f_DeleteTextures(GLsizei n, const GLuint *o) { (void)o; fake_gl.live_textures -= n; }
static GLuint APIENTRY f_CreateShader(GLenum t) { (void)t; fake_gl.live_shaders++; return next_name++; }
static GLuint APIENTRY f_CreateProgram(void) { fake_gl.live_programs++; return next_name++; }
static void APIENTRY f_DeleteShader(GLuint s) { (void)s; fake_gl.live_shaders--; }
static void APIENTRY f_DeleteProgram(GLuint p) { (void)p; fake_gl.live_programs--; }
static void APIENTRY f_GetShaderiv(GLuint s, GLenum p, GLint *o) { (void)s; (void)p; *o = GL_TRUE; }
static void APIENTRY f_GetProgramiv(GLuint s, GLenum p, GLint *o) { (void)s; (void)p; *o = GL_TRUE; }
static void APIENTRY f_GetInfoLog(GLuint s, GLsizei n, GLsizei *l, GLchar *o) { (void)s; if (l) *l = 0; if (n > 0 && o) o[0] = 0; }
static GLint APIENTRY f_GetUniformLocation(GLuint p, const GLchar *n) { (void)p; (void)n; return 0; }

/* ---- silent no-ops ---- */
static void APIENTRY f_enum(GLenum a) { (void)a; }
static void APIENTRY f_uint(GLuint a) { (void)a; }
static void APIENTRY f_enum_enum(GLenum a, GLenum b) { (void)a; (void)b; }
static void APIENTRY f_enum_uint(GLenum a, GLuint b) { (void)a; (void)b; }
static void APIENTRY f_uint_uint(GLuint a, GLuint b) { (void)a; (void)b; }
static void APIENTRY f_TexParameteri(GLenum a, GLenum b, GLint c) { (void)a; (void)b; (void)c; }
static void APIENTRY f_VertexAttribPointer(GLuint a, GLint b, GLenum c, GLboolean d, GLsizei e, const void *f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
static void APIENTRY f_VertexAttribIPointer(GLuint a, GLint b, GLenum c, GLsizei e, const void *f) { (void)a; (void)b; (void)c; (void)e; (void)f; }
static void APIENTRY f_Uniform1i(GLint a, GLint b) { (void)a; (void)b; }
static void APIENTRY f_Uniform1ui(GLint a, GLuint b) { (void)a; (void)b; }
static void APIENTRY f_Uniform1f(GLint a, GLfloat b) { (void)a; (void)b; }
static void APIENTRY f_Uniform2f(GLint a, GLfloat b, GLfloat c) { (void)a; (void)b; (void)c; }
static void APIENTRY f_Uniform3f(GLint a, GLfloat b, GLfloat c, GLfloat d) { (void)a; (void)b; (void)c; (void)d; }
static void APIENTRY f_Uniform4f(GLint a, GLfloat b, GLfloat c, GLfloat d, GLfloat e) { (void)a; (void)b; (void)c; (void)d; (void)e; }
static void APIENTRY f_UniformMatrix4fv(GLint a, GLsizei b, GLboolean c, const GLfloat *d) { (void)a; (void)b; (void)c; (void)d; }
static void APIENTRY f_DepthMask(GLboolean a) { (void)a; }
static void APIENTRY f_Clear(GLbitfield a) { (void)a; }
static void APIENTRY f_Viewport(GLint a, GLint b, GLsizei c, GLsizei d) { (void)a; (void)b; (void)c; (void)d; }
static void APIENTRY f_ClearColor(GLfloat a, GLfloat b, GLfloat c, GLfloat d) { (void)a; (void)b; (void)c; (void)d; }
static void APIENTRY f_TexImage2D(GLenum a, GLint b, GLint c, GLsizei d, GLsizei e, GLint f, GLenum g, GLenum h, const void *i) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; (void)i; }
static void APIENTRY f_ShaderSource(GLuint a, GLsizei b, const GLchar *const *c, const GLint *d) { (void)a; (void)b; (void)c; (void)d; }
static void APIENTRY f_BindAttribLocation(GLuint a, GLuint b, const GLchar *c) { (void)a; (void)b; (void)c; }

void fake_gl_install(void) {
    glDrawElements = f_DrawElements;
    glBufferData = f_BufferData;
    glGenBuffers = f_GenBuffers;
    glGenVertexArrays = f_GenVertexArrays;
    glGenTextures = f_GenTextures;
    glDeleteBuffers = f_DeleteBuffers;
    glDeleteVertexArrays = f_DeleteVertexArrays;
    glDeleteTextures = f_DeleteTextures;
    glCreateShader = f_CreateShader;
    glCreateProgram = f_CreateProgram;
    glDeleteShader = f_DeleteShader;
    glDeleteProgram = f_DeleteProgram;
    glGetShaderiv = f_GetShaderiv;
    glGetProgramiv = f_GetProgramiv;
    glGetShaderInfoLog = f_GetInfoLog;
    glGetProgramInfoLog = f_GetInfoLog;
    glGetUniformLocation = f_GetUniformLocation;

    glEnable = f_enum;
    glDisable = f_enum;
    glDepthFunc = f_enum;
    glCullFace = f_enum;
    glActiveTexture = f_enum;
    glUseProgram = f_uint;
    glLinkProgram = f_uint;
    glCompileShader = f_uint;
    glBindVertexArray = f_uint;
    glEnableVertexAttribArray = f_uint;
    glPolygonMode = f_enum_enum;
    glBlendFunc = f_enum_enum;
    glBindTexture = f_enum_uint;
    glBindBuffer = f_enum_uint;
    glAttachShader = f_uint_uint;
    glTexParameteri = f_TexParameteri;
    glVertexAttribPointer = f_VertexAttribPointer;
    glVertexAttribIPointer = f_VertexAttribIPointer;
    glUniform1i = f_Uniform1i;
    glUniform1ui = f_Uniform1ui;
    glUniform1f = f_Uniform1f;
    glUniform2f = f_Uniform2f;
    glUniform3f = f_Uniform3f;
    glUniform4f = f_Uniform4f;
    glUniformMatrix4fv = f_UniformMatrix4fv;
    glDepthMask = f_DepthMask;
    glClear = f_Clear;
    glViewport = f_Viewport;
    glClearColor = f_ClearColor;
    glTexImage2D = f_TexImage2D;
    glShaderSource = f_ShaderSource;
    glBindAttribLocation = f_BindAttribLocation;
}

/* ---- GLFW: no window is ever created ---- */
int glfwInit(void) { return 0; }
void glfwTerminate(void) {}
void glfwWindowHint(int h, int v) { (void)h; (void)v; }
GLFWwindow *glfwCreateWindow(int w, int h, const char *t, GLFWmonitor *m, GLFWwindow *s) { (void)w; (void)h; (void)t; (void)m; (void)s; return NULL; }
void glfwMakeContextCurrent(GLFWwindow *w) { (void)w; }
GLFWglproc glfwGetProcAddress(const char *n) { (void)n; return NULL; }
GLFWerrorfun glfwSetErrorCallback(GLFWerrorfun cb) { (void)cb; return NULL; }
GLFWframebuffersizefun glfwSetFramebufferSizeCallback(GLFWwindow *w, GLFWframebuffersizefun cb) { (void)w; (void)cb; return NULL; }
GLFWcursorposfun glfwSetCursorPosCallback(GLFWwindow *w, GLFWcursorposfun cb) { (void)w; (void)cb; return NULL; }
GLFWkeyfun glfwSetKeyCallback(GLFWwindow *w, GLFWkeyfun cb) { (void)w; (void)cb; return NULL; }
GLFWmousebuttonfun glfwSetMouseButtonCallback(GLFWwindow *w, GLFWmousebuttonfun cb) { (void)w; (void)cb; return NULL; }
void glfwSwapInterval(int i) { (void)i; }
int glfwWindowShouldClose(GLFWwindow *w) { (void)w; return 1; }
void glfwSwapBuffers(GLFWwindow *w) { (void)w; }
void glfwPollEvents(void) {}
static int fake_cursor_mode = GLFW_CURSOR_NORMAL;
void glfwSetInputMode(GLFWwindow *w, int m, int v) { (void)w; if (m == GLFW_CURSOR) fake_cursor_mode = v; }
int glfwGetInputMode(GLFWwindow *w, int m) { (void)w; return m == GLFW_CURSOR ? fake_cursor_mode : 0; }
