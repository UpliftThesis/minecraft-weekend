UNAME_S := $(shell uname -s)

CC = clang
CMAKE = cmake
CFLAGS = -std=c11 -O3 -g -Wall -Wextra -Wpedantic -Wstrict-aliasing
CFLAGS += -Wno-pointer-arith -Wno-newline-eof -Wno-unused-parameter -Wno-gnu-statement-expression
CFLAGS += -Wno-gnu-compound-literal-initializer -Wno-gnu-zero-variadic-macro-arguments
CFLAGS += -Ilib/cglm/include -Ilib/glad/include -Ilib/glfw/include -Ilib/stb -Ilib/noise -fbracket-depth=1024

# All generated files live under these two ignored directories; nothing is
# written into lib/ or src/.
BUILD = build
BIN = bin

# Dependencies. CGLM is used header-only (the game makes no glmc_ calls), so
# libcglm.a is neither built nor linked.
GLAD_OBJ  = $(BUILD)/glad/glad.o
NOISE_LIB = $(BUILD)/noise/libnoise.a
GLFW_DIR  = $(BUILD)/glfw
GLFW_LIB  = $(GLFW_DIR)/src/libglfw3.a
GLFW_CACHE = $(GLFW_DIR)/CMakeCache.txt
GLFW_SRC  = $(wildcard lib/glfw/CMakeLists.txt lib/glfw/src/CMakeLists.txt lib/glfw/src/*.c \
                       lib/glfw/src/*.m lib/glfw/src/*.h lib/glfw/include/GLFW/*.h)
GLFW_CMAKE_FLAGS = -DBUILD_SHARED_LIBS=OFF -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF \
                   -DGLFW_BUILD_DOCS=OFF -DGLFW_INSTALL=OFF
LIBS = $(GLAD_OBJ) $(GLFW_LIB) $(NOISE_LIB)

# The game's CFLAGS/LDFLAGS must not leak into GLFW's CMake configure step
# (make exports them when they come from the environment or command line).
unexport CFLAGS LDFLAGS

LDFLAGS = $(LIBS) -lm

# GLFW required frameworks on OSX
ifeq ($(UNAME_S), Darwin)
	LDFLAGS += -framework OpenGL -framework IOKit -framework CoreVideo -framework Cocoa
endif

ifeq ($(UNAME_S), Linux)
	LDFLAGS += -ldl -lpthread
endif

SRC = $(wildcard src/**/*.c) $(wildcard src/*.c) $(wildcard src/**/**/*.c) $(wildcard src/**/**/**/*.c)
OBJ = $(SRC:%.c=$(BUILD)/%.o)
DEP = $(OBJ:.o=.d)

.PHONY: all game libs run clean

all: $(BIN)/game

game: $(BIN)/game

libs: $(LIBS)

run: $(BIN)/game
	$(BIN)/game

$(BIN)/game: $(OBJ) $(LIBS)
	@mkdir -p $(@D)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

# -MMD -MP records header dependencies so header edits rebuild the right objects.
$(BUILD)/src/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) -MMD -MP -o $@ -c $< $(CFLAGS)

# GLAD and libnoise are compiled with the same (flagless) commands their
# in-source builds used, but into $(BUILD).
$(GLAD_OBJ): lib/glad/src/glad.c lib/glad/include/glad/glad.h lib/glad/include/KHR/khrplatform.h
	@mkdir -p $(@D)
	$(CC) -o $@ -Ilib/glad/include -c $<

$(BUILD)/noise/noise1234.o: lib/noise/noise1234.c lib/noise/noise1234.h
	@mkdir -p $(@D)
	$(CC) -o $@ -c $<

$(NOISE_LIB): $(BUILD)/noise/noise1234.o
	rm -f $@
	ar rcs $@ $^

# GLFW: out-of-source CMake build of the static library only.
$(GLFW_CACHE): lib/glfw/CMakeLists.txt
	$(CMAKE) -S lib/glfw -B $(GLFW_DIR) $(GLFW_CMAKE_FLAGS) || { rm -f $@; exit 1; }

$(GLFW_LIB): $(GLFW_CACHE) $(GLFW_SRC)
	$(CMAKE) --build $(GLFW_DIR) --target glfw
	@touch $@

clean:
	rm -rf $(BUILD) $(BIN)

-include $(DEP)
