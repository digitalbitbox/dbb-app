DIST_SUBDIRS = libbtc univalue
AM_CPPFLAGS = -I$(top_srcdir)/include
AM_LDFLAGS = $(PTHREAD_CFLAGS) $(LIBTOOL_LDFLAGS) -static

DBB_INCLUDES = 

LIBBTC=libbtc/libbtc.la
UNIVALUE=univalue/libunivalue.la

$(LIBBTC): $(wildcard libbtc/src/*) $(wildcard libbtc/include/*)
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) -C $(@D) $(@F)

DBB_INCLUDES += -I$(srcdir)/libbtc/include -I$(srcdir)/libbtc/src

$(UNIVALUE): $(wildcard univalue/lib/*) $(wildcard univalue/include/*)
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) -C $(@D) $(@F)

DBB_INCLUDES += -I$(srcdir)/univalue/include


noinst_LIBRARIES = libdbb.a libbpwalletclient.a

libdbb_a_CPPFLAGS = $(AM_CPPFLAGS) $(DBB_INCLUDES)
libdbb_a_INCLUDES = ../include/dbb.h libdbb/dbb_util.h libdbb/crypto.h
libdbb_a_SOURCES = libdbb/dbb.cpp libdbb/base64.cpp libdbb/crypto.cpp libdbb/dbb_util.h

libbpwalletclient_a_INCLUDES = libbitpay-wallet-client/bpwalletclient.h
libbpwalletclient_a_SOURCES = libbitpay-wallet-client/bpwalletclient.cpp
libbpwalletclient_a_CPPFLAGS = $(AM_CPPFLAGS) $(DBB_INCLUDES)
libbpwalletclient_a_LIBADD = $(LIBBTC)
 
dbb_cli_CONFIG_INCLUDES=-I$(builddir)/config
dbb_cli_INCLUDES=-I$(builddir) -I$(builddir)/obj

bin_PROGRAMS = dbb-cli

dbb_cli_SOURCES = dbb_cli.cpp dbb_util.h dbb_util.cpp
dbb_cli_CPPFLAGS = $(AM_CPPFLAGS) $(DBB_INCLUDES)
dbb_cli_CFLAGS =
dbb_cli_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)
dbb_cli_LDADD = $(UNIVALUE) libdbb.a $(CRYPTO_LIBS)


#check if we should build the dbb app
if ENABLE_DBB_APP
bin_PROGRAMS += dbb-app

dbb_app_CONFIG_INCLUDES=-I$(builddir)/config
dbb_app_SOURCES = dbb_app.h dbb_app.cpp dbb_util.h dbb_util.cpp
dbb_app_CPPFLAGS = -fPIC $(AM_CPPFLAGS) $(QR_CFLAGS) $(DBB_INCLUDES)
dbb_app_CFLAGS =
dbb_app_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS) $(LIBEVENT_LDFLAGS)
dbb_app_LDADD = libdbb.a libbpwalletclient.a $(LIBBTC) $(LIBEVENT_LIBS) $(CRYPTO_LIBS) $(LINUX_LIBS) $(UNIVALUE) $(BOOST_LIBS)

if ENABLE_QT

QT_FORMS_UI = \
  qt/ui/overview.ui \
	qt/ui/seeddialog.ui \
	qt/ui/backupdialog.ui
	
DBB_GUI_H = \
	qt/seeddialog.h

DBB_GUI_CPP = \
	qt/seeddialog.cpp \
    qt/backupdialog.cpp

QT_MOC_CPP = \
	qt/moc_dbb_gui.cpp \
	qt/moc_seeddialog.cpp \
    qt/moc_backupdialog.cpp

QT_MOC = \
  qt/dbb_gui.moc \
  qt/seeddialog.moc \
  qt/backupdialog.moc

QT_FORMS_H=$(join $(dir $(QT_FORMS_UI)),$(addprefix ui_, $(notdir $(QT_FORMS_UI:.ui=.h))))

$(QT_MOC): $(QT_FORMS_H)
$(dbb_app_OBJECTS) $(dbb_app_OBJECTS) : | $(QT_MOC)

dbb_app_SOURCES += qt/dbb_gui.h $(DBB_GUI_H) qt/dbb_gui.cpp qt/qrc_dbbgui.cpp $(QT_MOC_CPP) $(QT_MOC) $(QT_FORMS_UI) $(DBB_GUI_CPP) 
dbb_app_CPPFLAGS += $(QT_INCLUDES) $(QT_DBUS_INCLUDES)
dbb_app_LDFLAGS += $(QT_LDFLAGS)
dbb_app_LDADD += $(QT_LIBS) $(QT_DBUS_LIBS) $(QR_LIBS) -lcurl

CLEAN_QT = $(QT_MOC_CPP) $(QT_FORMS_H)

CLEANFILES = $(CLEAN_QT)

dbb_app_clean: FORCE
	rm -f $(CLEAN_QT) dbb-app$(EXEEXT)

clean-local:
	-$(MAKE) -C libbtc clean

ui_%.h: %.ui
	@test -f $(UIC)
	@$(MKDIR_P) $(@D)
	$(UIC) -o $@ $< || (echo "Error creating $@"; false)

qt/qrc_%.cpp: qt/%.qrc
	$(RCC) -o $@ $< || (echo "Error creating $@"; false)

qt/moc_%.cpp: qt/%.h
	$(MOC) -o $@ $< || (echo "Error creating $@"; false)

endif

endif
