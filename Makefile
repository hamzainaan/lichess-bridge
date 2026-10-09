TARGET   ?= native
CXXFLAGS ?= -O2

WINDOWS_TRIPLE ?= x86_64-w64-mingw32

SRC_DIR := src
SRCS    := $(wildcard $(SRC_DIR)/*.cpp)

ifeq ($(TARGET),windows)
ifeq ($(origin CXX),default)
CXX             := $(WINDOWS_TRIPLE)-g++
endif
WINDOWS_PREFIX  ?= /usr/$(WINDOWS_TRIPLE)/sys-root/mingw
BINARY          := bridge.exe
BUILD_DIR       := build/windows
CXXSTD          := -std=c++23 -pthread
WARNINGS        := -Wall -Wextra
TARGET_CPPFLAGS := -I$(WINDOWS_PREFIX)/include
TARGET_LDLIBS   := -L$(WINDOWS_PREFIX)/lib -static \
	                   -lssl -lcrypto -lz -lws2_32 -lcrypt32 -lbcrypt \
	                   -lpathcch -lshlwapi -lwinpthread
else
CXX             ?= g++
BINARY          := bridge
BUILD_DIR       := build
CXXSTD          := -std=c++23 -pthread
WARNINGS        := -Wall -Wextra
TARGET_CPPFLAGS :=
TARGET_LDLIBS   := -lssl -lcrypto -pthread
endif

OBJ_DIR  := $(BUILD_DIR)/obj
OBJS     := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))
DEPS     := $(OBJ_DIR)/*.d

CPPFLAGS := -MMD -MP $(TARGET_CPPFLAGS)

all: $(BINARY)

windows:
	$(MAKE) TARGET=windows all

$(BINARY): $(OBJS)
	$(CXX) $(CXXSTD) $(CXXFLAGS) -o $@ $(OBJS) $(TARGET_LDLIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXSTD) $(WARNINGS) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf build bridge bridge.exe

-include $(DEPS)

.PHONY: all windows clean
