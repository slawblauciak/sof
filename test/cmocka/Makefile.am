check_PROGRAMS =

LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) $(top_srcdir)/tap-driver.sh

if BUILD_XTENSA
LOG_COMPILER = xt-run
endif

AM_CFLAGS = \
	$(SOF_INCDIR) -Wall -Werror -g

AM_LDFLAGS =

if HAVE_CMOCKA_PREFIX
AM_CFLAGS += -I$(CMOCKA_PREFIX)/include
AM_LDFLAGS += -L$(CMOCKA_PREFIX)/lib
endif

if BUILD_XTENSA
AM_CFLAGS += -I../../src/arch/xtensa/include
AM_CFLAGS += -I../../src/platform/apollolake/include
AM_CFLAGS += -I../../src/audio
endif

if BUILD_HOST
AM_CFLAGS += -I../../src/arch/host/include
endif

CFLAGS := $(filter-out -nostdlib,$(CFLAGS))
LDFLAGS := $(filter-out -nostdlib,$(LDFLAGS))

LDADD = -lcmocka

# mixer tests
check_PROGRAMS += mixer_new
mixer_new_SOURCES = src/audio/mixer/mixer_new.c \
					src/audio/mixer/mock.c \
					src/audio/mixer/comp_mock.c \
					../../src/audio/buffer.c \
					../../src/audio/mixer.c
mixer_new_LDADD = -lm $(LDADD)

# all our binaries are test cases
TESTS = $(check_PROGRAMS)