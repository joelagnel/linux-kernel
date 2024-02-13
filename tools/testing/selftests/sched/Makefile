# SPDX-License-Identifier: GPL-2.0
TEST_GEN_PROGS := cs_dlserver_test
TEST_GEN_PROGS += cs_prctl_test

CFLAGS += $(KHDR_INCLUDES)
CFLAGS += -Wall

include ../lib.mk

$(OUTPUT)/cs_dlserver_test: cs_dlserver_test.c common.c
$(OUTPUT)/cs_prctl_test: cs_prctl_test.c common.c