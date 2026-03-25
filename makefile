.SUFFIXES:
.SILENT:

# Build configuration options
CM_OPT_CC_FLAGS ?=
CM_OPT_PANIC_NAME ?= panic_impl
CM_OPT_RELEASE ?= 1
CM_OPT_ENABLE_UBSAN ?= 0
CM_OPT_ENABLE_ASAN ?= 0
CM_OPT_ENABLE_SSE2 ?= 0
CM_OPT_STANDALONE ?= 1

CC ?= gcc
PRINTF ?= printf
RM_FLAGS = -f

# Project root directory
CM_DIR ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

# Target: cm (cheesemap.o)
cm = $(CM_DIR)/cheesemap.o
cm_SOURCE = $(CM_DIR)/cheesemap.c
cm_DEPEND = $(cm_SOURCE:.c=.d)

cm_CFLAGS = -std=gnu11 \
	-Wall -Wextra -Werror \
	-MMD -MP -I$(CM_DIR)

cm_CFLAGS += $(CM_OPT_CC_FLAGS)
cm_CFLAGS += -DCM_OPT_PANIC_NAME='$(CM_OPT_PANIC_NAME)'

ifeq ($(CM_OPT_RELEASE),1)
	cm_CFLAGS += -O2 -fno-stack-protector
else
	cm_CFLAGS += -g3
endif

ifeq ($(CM_OPT_ENABLE_UBSAN),1)
	cm_CFLAGS += -fsanitize=undefined
endif

ifeq ($(CM_OPT_ENABLE_ASAN),1)
	cm_CFLAGS += -fsanitize=address
endif

ifeq ($(CM_OPT_ENABLE_SSE2),1)
	cm_CFLAGS += -DCM_OPT_ENABLE_SSE2=1 -msse2
endif

# Target: cm_demo
cm_demo = $(CM_DIR)/cm-demo
cm_demo_SOURCE = $(CM_DIR)/cm-demo.c
cm_demo_DEPEND = $(cm_demo_SOURCE:.c=.d)
cm_demo_CFLAGS = $(cm_CFLAGS)

-include $(cm_DEPEND) $(cm_demo_DEPEND)

ifeq ($(CM_OPT_STANDALONE),1)
.PHONY: all
all: $(cm) $(cm_demo)
endif

$(cm): $(cm_SOURCE)
	@$(PRINTF) "  CC %s\n" "$(notdir $@)"
	$(CC) $(cm_CFLAGS) -c $< -o $@

$(cm_demo): $(cm_demo_SOURCE) $(cm)
	@$(PRINTF) "  CC %s\n" "$(notdir $@)"
	$(CC) $(cm_demo_CFLAGS) $^ -o $@

.PHONY: clean
clean::
	@$(PRINTF) "  RM %s\n" "$(notdir $(cm))"
	$(RM) $(RM_FLAGS) $(cm) $(cm_DEPEND)
	@$(PRINTF) "  RM %s\n" "$(notdir $(cm_demo))"
	$(RM) $(RM_FLAGS) $(cm_demo) $(cm_demo_DEPEND)
