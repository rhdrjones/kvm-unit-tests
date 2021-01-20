SHELL := /usr/bin/env bash

ifeq ($(wildcard config.mak),)
$(error run ./configure first. See ./configure -h)
endif

include config.mak

# Set search path for all sources
VPATH = $(SRCDIR)

libdirs-get = $(shell [ -d "lib/$(1)" ] && echo "lib/$(1) lib/$(1)/asm")
ARCH_LIBDIRS := $(call libdirs-get,$(ARCH)) $(call libdirs-get,$(TEST_DIR))
OBJDIRS := $(ARCH_LIBDIRS)

DESTDIR := $(PREFIX)/share/kvm-unit-tests/

.PHONY: arch_clean clean distclean cscope

# cc-option
# Usage: OP_CFLAGS+=$(call cc-option, -falign-functions=0, -malign-functions=0)
cc-option = $(shell if $(CC) -Werror $(1) -S -o /dev/null -xc /dev/null \
              > /dev/null 2>&1; then echo "$(1)"; else echo "$(2)"; fi ;)

#make sure env CFLAGS variable is not used
CFLAGS =

libgcc := $(shell $(CC) --print-libgcc-file-name)

libcflat := lib/libcflat.a
cflatobjs := \
	lib/argv.o \
	lib/printf.o \
	lib/string.o \
	lib/abort.o \
	lib/report.o \
	lib/stack.o

# libfdt paths
LIBFDT_objdir = lib/libfdt
LIBFDT_srcdir = $(SRCDIR)/lib/libfdt
LIBFDT_archive = $(LIBFDT_objdir)/libfdt.a
LIBFDT_include = $(addprefix $(LIBFDT_srcdir)/,$(LIBFDT_INCLUDES))
LIBFDT_version = $(addprefix $(LIBFDT_srcdir)/,$(LIBFDT_VERSION))

OBJDIRS += $(LIBFDT_objdir)

# EFI App
ifeq ($(TARGET_EFI),y)
EFI_INCLUDES = -I /usr/include/efi
ifeq ($(ARCH_NAME),aarch64)
EFI_INCLUDES += -I /usr/include/efi/aa64
EFI_INCLUDES += -I /usr/include/efi/aarch64
else ifeq ($(ARCH_NAME),x86_64)
EFI_INCLUDES += -I /usr/include/efi/x64
else
$(error Cannot build $(ARCH_NAME) tests as EFI apps)
endif
EFI_CRTOBJ = $(TEST_DIR)/efi/crt0-efi-$(ARCH_NAME).o
EFI_CFLAGS = -DTARGET_EFI -DEFI_DEBUG $(EFI_INCLUDES) \
	     -fpic -fshort-wchar -fno-stack-check -fno-merge-all-constants \
	     -Wno-error=missing-prototypes -Wno-error=strict-prototypes -Wno-error=pragmas
EFI_LDFLAGS = -nostdlib --defsym=EFI_SUBSYSTEM=0xa -shared -Bsymbolic $(EFI_CRTOBJ) \
	      --warn-common --no-undefined --fatal-warnings --build-id=sha1
EFI_LIBS = -lgnuefi -lefi
efi_clean = efi_clean
.PRECIOUS: %.so
endif

#include architecture specific make rules
include $(SRCDIR)/$(TEST_DIR)/Makefile

COMMON_CFLAGS += -g $(autodepend-flags) -fno-strict-aliasing -fno-common
COMMON_CFLAGS += -Wall -Wwrite-strings -Wempty-body -Wuninitialized
COMMON_CFLAGS += -Wignored-qualifiers -Werror

frame-pointer-flag=-f$(if $(KEEP_FRAME_POINTER),no-,)omit-frame-pointer
fomit_frame_pointer := $(call cc-option, $(frame-pointer-flag), "")
fno_stack_protector := $(call cc-option, -fno-stack-protector, "")
fno_stack_protector_all := $(call cc-option, -fno-stack-protector-all, "")
wno_frame_address := $(call cc-option, -Wno-frame-address, "")
fno_pic := $(call cc-option, -fno-pic, "")
no_pie := $(call cc-option, -no-pie, "")
wclobbered := $(call cc-option, -Wclobbered, "")
wunused_but_set_parameter := $(call cc-option, -Wunused-but-set-parameter, "")
wmissing_parameter_type := $(call cc-option, -Wmissing-parameter-type, "")
wold_style_declaration := $(call cc-option, -Wold-style-declaration, "")

COMMON_CFLAGS += $(fomit_frame_pointer)
COMMON_CFLAGS += $(fno_stack_protector)
COMMON_CFLAGS += $(fno_stack_protector_all)
COMMON_CFLAGS += $(wno_frame_address)
COMMON_CFLAGS += $(if $(U32_LONG_FMT),-D__U32_LONG_FMT__,)
COMMON_CFLAGS += $(wclobbered)
COMMON_CFLAGS += $(wunused_but_set_parameter)
ifeq ($(TARGET_EFI),y)
COMMON_CFLAGS += $(EFI_CFLAGS)
else
COMMON_CFLAGS += $(fno_pic) $(no_pie)
endif

CFLAGS += $(COMMON_CFLAGS)
CFLAGS += $(wmissing_parameter_type)
CFLAGS += $(wold_style_declaration)
CFLAGS += -Woverride-init -Wmissing-prototypes -Wstrict-prototypes

autodepend-flags = -MMD -MF $(dir $*).$(notdir $*).d

LDFLAGS += $(CFLAGS)

$(libcflat): $(cflatobjs)
	$(AR) rcs $@ $^

include $(LIBFDT_srcdir)/Makefile.libfdt
$(LIBFDT_archive): CFLAGS += -ffreestanding -I $(SRCDIR)/lib -I $(SRCDIR)/lib/libfdt -Wno-sign-compare
$(LIBFDT_archive): $(addprefix $(LIBFDT_objdir)/,$(LIBFDT_OBJS))
	$(AR) rcs $@ $^


# Build directory target
.PHONY: directories
directories:
	@mkdir -p $(OBJDIRS)

%.o: %.S
	$(CC) $(CFLAGS) -c -nostdlib -o $@ $<

-include */.*.d */*/.*.d

all: directories $(shell cd $(SRCDIR) && git rev-parse --verify --short=8 HEAD >$(PWD)/build-head 2>/dev/null)

standalone: all
	@scripts/mkstandalone.sh

efi: all
	@scripts/mkefi.sh

install: standalone
	mkdir -p $(DESTDIR)
	install tests/* $(DESTDIR)

clean: arch_clean $(efi_clean)
	$(RM) lib/.*.d $(libcflat) $(cflatobjs)

libfdt_clean:
	$(RM) $(LIBFDT_archive) \
	$(addprefix $(LIBFDT_objdir)/,$(LIBFDT_OBJS)) \
	$(LIBFDT_objdir)/.*.d

efi_clean:
	$(RM) $(TEST_DIR)/*.so $(TEST_DIR)/*.efi lib/efi.o

distclean: clean libfdt_clean
	$(RM) lib/asm lib/config.h config.mak $(TEST_DIR)-run msr.out cscope.* build-head
	$(RM) -r tests efi-tests logs logs.old

cscope: cscope_dirs = lib lib/libfdt lib/linux $(TEST_DIR) $(ARCH_LIBDIRS) lib/asm-generic
cscope:
	$(RM) ./cscope.*
	find -L $(cscope_dirs) -maxdepth 1 \
		-name '*.[chsS]' -exec realpath --relative-base=$(PWD) {} \; | sort -u > ./cscope.files
	cscope -bk
