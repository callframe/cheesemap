CM_OPT_RELEASE ?= 0
CM_OPT_CC_FLAGS ?=
CM_OPT_ASSERT_PATH ?= <assert.h>
CM_OPT_ENABLE_DEMO ?= 1
CM_OPT_ENABLE_UBSAN ?= 0
CM_OPT_ENABLE_ASAN ?= 0

CC ?= gcc

CM_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

CM_SOURCE := $(CM_DIR)/cheesemap.c
CM_OBJECT := $(CM_SOURCE:.c=.o)
CM_DEPEND := $(CM_SOURCE:.c=.d)

CM_DEMO_SOURCE := $(CM_DIR)/cm-demo.c
CM_DEMO := $(CM_DEMO_SOURCE:.c=)
CM_DEMO_DEPEND := $(CM_DEMO_SOURCE:.c=.d)

CM_CC_FLAGS := \
	-Wall -Wextra -Werror \
	-MMD -MP -I$(CM_DIR)

ifeq ($(CM_OPT_RELEASE),1)
	CM_CC_FLAGS += -O2 -fno-stack-protector
else
	CM_CC_FLAGS += -g3
endif

CM_CC_FLAGS += $(CM_OPT_CC_FLAGS)
CM_CC_FLAGS += -DCM_OPT_ASSERT_PATH='$(CM_OPT_ASSERT_PATH)'

ifeq ($(CM_OPT_ENABLE_UBSAN),1)
	CM_CC_FLAGS += -fsanitize=undefined
endif

ifeq ($(CM_OPT_ENABLE_ASAN),1)
	CM_CC_FLAGS += -fsanitize=address
endif

.PHONY: all
all:: $(CM_OBJECT)

$(CM_OBJECT): $(CM_SOURCE)
	$(CC) $(CM_CC_FLAGS) -c $< -o $@

ifeq ($(CM_OPT_ENABLE_DEMO),1)
.PHONY: all
all:: $(CM_DEMO)

$(CM_DEMO): $(CM_DEMO_SOURCE) $(CM_OBJECT)
	$(CC) $(CM_CC_FLAGS) $^ -o $@
endif

.PHONY: clean
clean::
	$(RM) $(CM_OBJECT) $(CM_DEPEND)
ifeq ($(CM_OPT_ENABLE_DEMO),1)
	$(RM) $(CM_DEMO) $(CM_DEMO_DEPEND)
endif

-include $(CM_DEPEND) $(CM_DEMO_DEPEND)
