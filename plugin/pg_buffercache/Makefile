# contrib/pg_buffercache/Makefile

MODULE_big = pg_buffercache
OBJS = \
	$(WIN32RES) \
	pg_buffercache_pages.o

EXTENSION = pg_buffercache
DATA = pg_buffercache--1.2.sql pg_buffercache--1.2--1.3.sql \
	pg_buffercache--1.1--1.2.sql pg_buffercache--1.0--1.1.sql
PGFILEDESC = "pg_buffercache - monitoring of shared buffer cache in real-time"

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
