MODULES = pilotscope
EXTENSION = pilotscope
MODULE_big = pilotscope
DATA = pilotscope--0.1.sql

OBJS = $(patsubst %.c,%.o,$(wildcard *.c))
OBJS += $(patsubst %.c,%.o,$(wildcard utils/*.c))
OBJS += $(patsubst %.c,%.o,$(wildcard optimizer/path/*.c))
OBJS += $(patsubst %.c,%.o,$(wildcard optimizer/plan/*.c))
OBJS += $(patsubst %.c,%.o,$(wildcard optimizer/util/*.c))

ifndef PG_CONFIG
	PG_CONFIG = pg_config
endif

# the env vars needed by the extension are in PGXS
PGXS := $(shell $(PG_CONFIG) --pgxs)

# Set the C compiler flags for PostgreSQL extension module build. Only use the local symbols rather than the global symbols
PG_CFLAGS := -Wl,-Bsymbolic-functions
PG_CPPFLAGS := -Wl,-Bsymbolic-functions

include $(PGXS)

