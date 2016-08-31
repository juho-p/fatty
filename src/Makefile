SRC := charset.c child.c config.c ctrls.c minibidi.c std.c term.c termclip.c \
	termline.c termmouse.c termout.c winclip.c winctrls.c windialog.c wininput.c \
	winmain.c winprint.c wintext.c wintip.c xcwidth.c \
	childxx.cc winxx.cc \
	res.rc
BIN ?= fatty.exe
CXX = g++
CC = gcc
RC = windres

CXXFLAGS ?= -Wall -Wextra -std=gnu++11
CFLAGS ?= -std=gnu99 -include std.h -Wall -Wextra -Wundef -Werror
LDFLAGS := -static-libgcc -mwindows -lcomctl32 -limm32 -lwinspool -lole32 -luuid -ld2d1

TARGET := $(shell $(CC) -dumpmachine)
CPPFLAGS := -DTARGET=$(TARGET) -D_GNU_SOURCE

BUILD_DIR = build/

LDFLAGS := -L$(shell $(CC) -print-file-name=w32api) $(LDFLAGS)

ifdef DEBUG
  extraflags := -g -O0
else
  extraflags := -Os
  CPPFLAGS += -DNDEBUG
endif
CFLAGS += $(extraflags)
CXXFLAGS += $(extraflags)

# ---------------------------

objfiles := $(addprefix $(BUILD_DIR), $(addsuffix .o, $(basename $(SRC))))
depfiles := $(objfiles:.o=.d)

$(BIN): $(objfiles)
	$(CXX) -o $(BIN) $(objfiles) $(LDFLAGS)

-include $(depfiles)

prepare_build = mkdir -pv $(dir $@)

compileflags = -c $< -o $@ -MMD -MT $@
$(BUILD_DIR)%.o: %.cc Makefile
	@$(prepare_build)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(compileflags)
$(BUILD_DIR)%.o: %.c Makefile
	@$(prepare_build)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(compileflags)
$(BUILD_DIR)%.o: %.rc
	@$(prepare_build)
	$(RC) --preprocessor '$(CC) -E -xc -DRC_INVOKED -MMD -MT $@ $(CPPFLAGS)' $< $*.o
	@mv -v res.o res.d $(BUILD_DIR)

.PHONY: clean
clean:
	-rm -rf build $(BIN)
