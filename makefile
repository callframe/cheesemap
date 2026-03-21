# Header in which assert(x) is defined
CM_OPT_RELEASE ?= 0
CM_OPT_CC_FLAGS ?=
CM_OPT_ASSERT_PATH ?= <assert.h>

CC ?= gcc

CM_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

CM_SOURCE := $(CM_DIR)/cheesemap.c
CM_OBJECT := $(CM_SOURCE:.c=.o)
CM_DEPEND := $(CM_SOURCE:.c=.d)

CM_CC_FLAGS := \
	-Wall -Wextra \
	-MMD -MP -I$(CM_DIR)

ifeq ($(CM_OPT_RELEASE),1)
	CM_CC_FLAGS += -O2 -fno-stack-protector
else
	CM_CC_FLAGS += -g3
endif

CM_CC_FLAGS += $(CM_OPT_CC_FLAGS)
CM_CC_FLAGS += -DCM_OPT_ASSERT_PATH='$(CM_OPT_ASSERT_PATH)'

.PHONY: all
all: $(CM_OBJECT)

$(CM_OBJECT): $(CM_SOURCE)
	$(CC) $(CM_CC_FLAGS) -c $< -o $@

.PHONY: clean
clean::
	$(RM) $(CM_OBJECT) $(CM_DEPEND)

-include $(CM_DEPEND)
