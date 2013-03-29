AM_LDFLAGS = \
	-Wl,--as-needed

data_DATA =

bin_PROGRAMS = \
	alsa-dsd-player

sbin_SCRIPTS =

alsa_dsd_player_SOURCES = \
	main.c

alsa_dsd_player_CPPFLAGS = \
	$(AM_CPPFLAGS) $(ALSA_CFLAGS) -Wall

alsa_dsd_player_LDADD = \
	$(ALSA_LIBS) -lm

man_MANS = 

EXTRA_DIST = 

distclean-local:
	rm -rf autom4te.cache
