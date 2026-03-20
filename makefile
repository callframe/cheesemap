# Header in which assert(x) is defined
CM_OPT_ASSERT_PATH ?= <assert.h>

CC ?= gcc

CM_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

CM_SOURCE := $(CM_DIR)/cheesemap.c
CM_OBJECT := $(CM_SOURCE:.c=.o)
CM_DEPEND := $(CM_SOURCE:.c=.d)

CM_CC_FLAGS := \
	-Wall -Wextra -pedantic \
	-MMD -MP -I$(CM_DIR)

CM_CC_FLAGS += -DCM_OPT_ASSERT_PATH='$(CM_OPT_ASSERT_PATH)'

.PHONY: all
all: $(CM_OBJECT)

$(CM_OBJECT): $(CM_SOURCE)
	$(CC) $(CM_CC_FLAGS) -c $< -o $@

.PHONY: clean
clean::
	$(RM) $(CM_OBJECT) $(CM_DEPEND)

-include $(CM_DEPEND)