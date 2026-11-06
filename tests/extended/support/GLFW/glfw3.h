/* Test-owned, minimal stand-in for <GLFW/glfw3.h>.
 * The GLFW submodule is not checked out and the tests run headless, so only
 * the declarations the game uses are provided (values match GLFW 3.3).
 * Implementations live in support/fake_platform.c. */
#ifndef TEST_FAKE_GLFW3_H
#define TEST_FAKE_GLFW3_H

#define GLFW_RELEASE 0
#define GLFW_PRESS 1

#define GLFW_KEY_SPACE 32
#define GLFW_KEY_0 48
#define GLFW_KEY_A 65
#define GLFW_KEY_D 68
#define GLFW_KEY_K 75
#define GLFW_KEY_L 76
#define GLFW_KEY_P 80
#define GLFW_KEY_S 83
#define GLFW_KEY_T 84
#define GLFW_KEY_W 87
#define GLFW_KEY_ESCAPE 256
#define GLFW_KEY_LEFT_SHIFT 340
#define GLFW_KEY_LAST 348

#define GLFW_MOUSE_BUTTON_LEFT 0
#define GLFW_MOUSE_BUTTON_RIGHT 1
#define GLFW_MOUSE_BUTTON_LAST 7

#define GLFW_RESIZABLE 0x00020003
#define GLFW_CONTEXT_VERSION_MAJOR 0x00022002
#define GLFW_CONTEXT_VERSION_MINOR 0x00022003
#define GLFW_OPENGL_FORWARD_COMPAT 0x00022006
#define GLFW_OPENGL_PROFILE 0x00022008
#define GLFW_OPENGL_CORE_PROFILE 0x00032001
#define GLFW_CURSOR 0x00033001
#define GLFW_CURSOR_NORMAL 0x00034001
#define GLFW_CURSOR_DISABLED 0x00034003

typedef struct GLFWwindow GLFWwindow;
typedef struct GLFWmonitor GLFWmonitor;
typedef void (*GLFWglproc)(void);
typedef void (*GLFWerrorfun)(int, const char *);
typedef void (*GLFWframebuffersizefun)(GLFWwindow *, int, int);
typedef void (*GLFWcursorposfun)(GLFWwindow *, double, double);
typedef void (*GLFWkeyfun)(GLFWwindow *, int, int, int, int);
typedef void (*GLFWmousebuttonfun)(GLFWwindow *, int, int, int);

int glfwInit(void);
void glfwTerminate(void);
void glfwWindowHint(int hint, int value);
GLFWwindow *glfwCreateWindow(int width, int height, const char *title, GLFWmonitor *monitor, GLFWwindow *share);
void glfwMakeContextCurrent(GLFWwindow *window);
GLFWglproc glfwGetProcAddress(const char *name);
GLFWerrorfun glfwSetErrorCallback(GLFWerrorfun cb);
GLFWframebuffersizefun glfwSetFramebufferSizeCallback(GLFWwindow *w, GLFWframebuffersizefun cb);
GLFWcursorposfun glfwSetCursorPosCallback(GLFWwindow *w, GLFWcursorposfun cb);
GLFWkeyfun glfwSetKeyCallback(GLFWwindow *w, GLFWkeyfun cb);
GLFWmousebuttonfun glfwSetMouseButtonCallback(GLFWwindow *w, GLFWmousebuttonfun cb);
void glfwSwapInterval(int interval);
int glfwWindowShouldClose(GLFWwindow *window);
void glfwSwapBuffers(GLFWwindow *window);
void glfwPollEvents(void);
void glfwSetInputMode(GLFWwindow *window, int mode, int value);
int glfwGetInputMode(GLFWwindow *window, int mode);

#endif
