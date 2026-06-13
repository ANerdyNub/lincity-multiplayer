## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS = foreign subdir-objects

### DEFS = @DEFS@ -DLIBDIR=\"${pkgdatadir}\"
DEFS = @DEFS@
X_DEFS = -DLC_X11=1 -Wall
SVGA_DEFS = -DSVGALIB=1

SUBDIRS = m4 intl po src/modules

## (deleted)
##	acconfig.h   \
##	config.h \

noinst_HEADERS = \
	src/cliglobs.h   \
	src/common.h     src/confw32.h    \
	src/engglobs.h   src/engine.h	 src/fileutil.h \
	src/generic.h    src/geometry.h  src/dialbox.h \
	src/lcconfig.h   \
	src/lchelp.h       \
	src/lcintl.h     src/lcstring.h	src/lclib.h\
	src/lctypes.h    src/lcwin32.h \
	src/lcx11.h      src/lcsvga.h \
	src/ldsvgui.h    src/ldsvguts.h   \
	src/lin-city.h   src/lintypes.h	 src/market.h \
	src/module_buttons.h \
	src/mouse.h      src/mps.h	  src/pbar.h    \
	src/picedit.h    src/pixmap.h     src/power.h \
	src/readpng.h \
	src/resource.h	 src/screen.h \
	src/shrglobs.h   src/stats.h \
	src/simulate.h   src/timer.h	 src/transport.h \
	src/typeinit.h   src/xpicedit.h	src/modules.h

# Directories that need to have CVS and ca subdirectories removed
LC_DISTDIRS = \
	help icons messages \
	old.Makefiles opening tools

##EXTRA_DIST = config.rpath  $(LC_DISTDIRS) \

EXTRA_DIST = $(LC_DISTDIRS) \
	colour.pal \
	lincity.ico  small.ico lincity.rc \
	CHANGES COPYRIGHT Acknowledgements \
	config.sh \
	README.profiling \
	lincity.6

man_MANS = lincity.6
## pkgdata_DATA = help/* icons/* messages/* messages/ca/* opening/* colour.pal
## pkgdata_DATA = help/* icons/* messages/* opening/* colour.pal
## nobase_pkgdata_DATA = help/* icons/* messages/ca/* messages/* opening/* colour.pal
nobase_pkgdata_DATA = help/* icons/* messages/* opening/* colour.pal


############################################################################
## INSTALL SCRIPTS
############################################################################
## There are three "non-standard" things that I need to do here.  
## 1)  I need to make the directories .../share/lincity/help, icons, 
##     messages and opening because automake is not smart enough to do this.
## 2)  I need to check to see if the user chose to make the optionally
##     compiled picedit/xpicedit, and install these.
## 3)  I need to set the suid bit on lincity/picedit.
############################################################################
###########   GCS Jan 19, 2003
############################################################################
##   1)  No longer need to make the directories.  Automake now supports the
##       "nobase" option.
##	    $(mkinstalldirs) $(DESTDIR)$(pkgdatadir)
##	    $(mkinstalldirs) $(DESTDIR)$(pkgdatadir)/help
##	    $(mkinstalldirs) $(DESTDIR)$(pkgdatadir)/icons
##	    $(mkinstalldirs) $(DESTDIR)$(pkgdatadir)/messages
##	    $(mkinstalldirs) $(DESTDIR)$(pkgdatadir)/opening
##   2)  Removed the following line from install-exec-local:
##	    $(mkinstalldirs) $(DESTDIR)$(pkgdatadir)/messages/ca
############################################################################

install-exec-local:
	@list='$(EXTRA_PROGRAMS)'; for p in $$list; do \
	  if test -f $$p; then \
	    echo "  $(INSTALL_PROGRAM) $(INSTALL_STRIP_FLAG) $$p $(DESTDIR)$(bindir)/`echo $$p|sed 's/$(EXEEXT)$$//'|sed '$(transform)'|sed 's/$$/$(EXEEXT)/'`"; \
	     $(INSTALL_PROGRAM) $(INSTALL_STRIP_FLAG) $$p $(DESTDIR)$(bindir)/`echo $$p|sed 's/$(EXEEXT)$$//'|sed '$(transform)'|sed 's/$$/$(EXEEXT)/'`; \
	  else :; fi; \
	done; \
        if test -f $(DESTDIR)$(bindir)/`echo lincity|sed 's/$(EXEEXT)$$//'|sed '$(transform)'|sed 's/$$/$(EXEEXT)/'`; then \
	  chown root $(DESTDIR)$(bindir)/`echo lincity|sed 's/$(EXEEXT)$$//'|sed '$(transform)'|sed 's/$$/$(EXEEXT)/'`; \
	  chmod 4755 $(DESTDIR)$(bindir)/`echo lincity|sed 's/$(EXEEXT)$$//'|sed '$(transform)'|sed 's/$$/$(EXEEXT)/'`; \
	fi

## Uninstall also removes the extra programs and directories
uninstall-local:
	list='$(EXTRA_PROGRAMS)'; for p in $$list; do \
	  rm -f $(DESTDIR)$(bindir)/`echo $$p|sed 's/$(EXEEXT)$$//'|sed '$(transform)'|sed 's/$$/$(EXEEXT)/'`; \
	done
	rm -rf $(DESTDIR)$(pkgdatadir)


############################################################################
## DISTRIBUTION BUILD SCRIPTS
## --
## This is needed to keep the CVS directories from being installed in 
## my subdirectories.
## --
## GCS Jan 19, 2003  Added a check for deleting the "ca" subdirectory
##                   Added lowercase "cvs" directory (when copied from dos)
##  (removed this!)
##          cp -r $$p $(distdir)/$$p; \
##  (modified this)
# 	  if test -d $(distdir)/$$p/CVS ; then \
# 	    echo "rm -rf $(distdir)/$$p/CVS" ; \
# 	    rm -rf $(distdir)/$$p/CVS ; \
# 	    echo "rm -rf $(distdir)/$$p/*/CVS" ; \
# 	    rm -rf $(distdir)/$$p/*/CVS ; \
# 	    echo "rm -rf $(distdir)/$$p/*/*/CVS" ; \
# 	    rm -rf $(distdir)/$$p/*/*/CVS ; \
# 	    echo "rm -rf $(distdir)/$$p/cvs" ; \
# 	    rm -rf $(distdir)/$$p/cvs ; \
# 	    echo "rm -rf $(distdir)/$$p/*/cvs" ; \
# 	    rm -rf $(distdir)/$$p/*/cvs ; \
# 	    echo "rm -rf $(distdir)/$$p/*/*/cvs" ; \
# 	    rm -rf $(distdir)/$$p/*/*/cvs ; \
# 	  else :; fi; \
##
##	  echo "Erasing CVS directories..." \
############################################################################
dist-hook:
	@list='$(LC_DISTDIRS)'; for p in $$list; do \
	  rm -rf `find $(distdir)/$$p -name CVS -print` ; \
	  rm -rf `find $(distdir)/$$p -name cvs -print` ; \
	  rm -rf `find $(distdir)/$$p -name ca -print` ; \
	done


############################################################################
## PROGRAM BUILD SCRIPTS
############################################################################

## These executables might not be built
EXTRA_PROGRAMS = xlincity lincity xpicedit picedit

EXTRA_LTLIBRARIES = liblcx.la liblcsvga.la
X_PROGS: xlincity$(EXEEXT)
SVGA_PROGS: lincity$(EXEEXT)
LCSERVER_PROGS: lcserver$(EXEEXT)

LCX_LIBS: liblcx.la
LCSVGA_LIBS: liblcsvga.la

## All sources are under src/, add to include path
AM_CPPFLAGS = -I$(top_srcdir)/src

## These executables are always built
bin_PROGRAMS = @PROGS@
lib_LTLIBRARIES = @LCLIBS@

## Note: If the source file makes use of any graphic routines, it should 
## be listed under gui_SOURCES, otherwise its safe for engine_SOURCES

## XXX: fileutil.c uses HandleError, a platform specific function, yet
## its compiled without appropriate -Ds.  Fixme post 1.13.1

gui_SOURCES =	src/ldsvgui.c src/splash.c src/lchelp.c src/main.c src/mouse.c \
	src/mps.c src/screen.c src/pixmap.c src/cliglobs.c src/geometry.c \
	src/pbar.c src/module_buttons.c src/dialbox.c src/network.c

engine_SOURCES = src/lclib.c src/power.c src/ldsvguts.c src/engine.c \
	src/transport.c src/market.c src/simulate.c src/shrtypes.c \
	src/lintypes.c src/readpng.c src/timer.c src/fileutil.c \
	src/shrglobs.c src/stats.c

liblcx_la_SHORTNAME = lcx
liblcx_la_SOURCES = $(gui_SOURCES) src/lcx11.c 
liblcx_la_LIBADD = @X_LIBS@ -lXext -lX11 -lpng -lz -lm
liblcx_la_CPPFLAGS = -I$(top_srcdir)/src $(X_DEFS)

liblcsvga_la_SHORTNAME = lcsvga
liblcsvga_la_SOURCES = $(gui_SOURCES) src/lcsvga.c
liblcsvga_la_LIBADD = @SVGA_LIBS@ -lvgagl -lvga -lpng -lz -lm
liblcsvga_la_CPPFLAGS = -I$(top_srcdir)/src $(SVGA_DEFS)


lincity_SOURCES = $(engine_SOURCES)
lincity_DEPENDENCIES = LCSVGA_LIBS src/modules/libmodules.la
lincity_LDADD = liblcsvga.la src/modules/libmodules.la

xlincity_SOURCES = $(engine_SOURCES)
xlincity_DEPENDENCIES = LCX_LIBS src/modules/libmodules.la 
xlincity_LDADD = liblcx.la src/modules/libmodules.la

## ----- xpicedit -----
xpicedit_SOURCES = 
EXTRA_xpicedit_SOURCES = src/picedit.c
xpicedit_LDADD = x_picedit.o -lXext -lX11 -lm

## ----- picedit -----
picedit_SOURCES = 
EXTRA_picedit_SOURCES = src/picedit.c
picedit_LDADD = vga_picedit.o -lvgagl -lvga -lm


## ----------------- PICEDIT RULES ------------------
x_picedit.o: src/picedit.c
	$(COMPILE) $(X_DEFS) -o x_picedit.o -c src/picedit.c

vga_picedit.o: src/picedit.c
	$(COMPILE) $(X_DEFS) -o vga_picedit.o -c src/picedit.c


ACLOCAL_AMFLAGS = -I m4
