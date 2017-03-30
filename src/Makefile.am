DIST_SUBDIRS = libbtc univalue hidapi
GIT_VERSION := $(shell git describe --abbrev=6 --always --tags)

AM_CPPFLAGS = -I$(top_srcdir)/include
AM_LDFLAGS = $(PTHREAD_CFLAGS) $(LIBTOOL_LDFLAGS) $(HARDENED_LDFLAGS)
AM_CXXFLAGS = $(HARDENED_CXXFLAGS)
AM_CPPFLAGS += $(HARDENED_CPPFLAGS) -DVERSION=\"$(GIT_VERSION)\"

DBB_INCLUDES = 

LIBBTC=libbtc/libbtc.la
UNIVALUE=univalue/libunivalue.la

if TARGET_WINDOWS
  HIDAPI=hidapi/windows/libhidapi.la
  HIDAPI_FIRMWARE=hidapi/windows/libhidapi.la
endif

if TARGET_OSX
  HIDAPI=hidapi/mac/libhidapi.la
  HIDAPI_FIRMWARE=hidapi/mac/libhidapi.la
endif

if TARGET_LINUX
  HIDAPI=hidapi/linux/libhidapi-hidraw.la
  HIDAPI_FIRMWARE=hidapi/libusb/libhidapi-libusb.la
endif

$(LIBBTC): $(wildcard libbtc/src/*) $(wildcard libbtc/include/*)
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) -C $(@D) $(@F)

LIBBTC_INCLUDES = -I$(srcdir)/libbtc/include -I$(srcdir)/libbtc/src

$(UNIVALUE): $(wildcard univalue/lib/*) $(wildcard univalue/include/*)
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) -C $(@D) $(@F)

$(HIDAPI):
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) -C $(@D) $(@F)

$(HIDAPI_FIRMWARE):
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) -C $(@D) $(@F)
	
HIDAPI_INCLUDES = -I$(srcdir)/hidapi
	
DBB_INCLUDES += -I$(srcdir)/univalue/include $(LIBBTC_INCLUDES) $(HIDAPI_INCLUDES)


noinst_LIBRARIES = libdbb.a libbpwalletclient.a

libdbb_a_CPPFLAGS = $(AM_CPPFLAGS) $(DBB_INCLUDES)
libdbb_a_INCLUDES = ../include/dbb.h libdbb/dbb_util.h libdbb/crypto.h $(HIDAPI_INCLUDES) $(LIBBTC_INCLUDES)
libdbb_a_SOURCES = libdbb/dbb.cpp libdbb/base64.cpp libdbb/crypto.cpp libdbb/dbb_util.h
libdbb_a_LIBADD = $(LIBBTC) $(HIDAPI)

libbpwalletclient_a_INCLUDES = bitpaywalletclient/bpwalletclient.h
libbpwalletclient_a_SOURCES = bitpaywalletclient/bpwalletclient.cpp
libbpwalletclient_a_CPPFLAGS = $(AM_CPPFLAGS) $(DBB_INCLUDES)
libbpwalletclient_a_LIBADD = $(HIDAPI) $(LIBBTC) $(UNIVALUE)
 
dbb_cli_CONFIG_INCLUDES=-I$(builddir)/config
dbb_cli_INCLUDES=-I$(builddir) -I$(builddir)/obj

bin_PROGRAMS = dbb-cli dbb-firmware

dbb_cli_SOURCES = dbb_cli.cpp dbb_util.h dbb_util.cpp
dbb_cli_CPPFLAGS = $(AM_CPPFLAGS) $(DBB_INCLUDES)
dbb_cli_CFLAGS =
dbb_cli_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)
dbb_cli_LDADD = $(UNIVALUE) libdbb.a $(LIBBTC) $(HIDAPI) 

dbb_firmware_SOURCES = dbb_firmware.cpp dbb_util.h dbb_util.cpp firmware.h
dbb_firmware_CPPFLAGS = $(AM_CPPFLAGS) $(DBB_INCLUDES)
dbb_firmware_CFLAGS =
dbb_firmware_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)
dbb_firmware_LDADD = libdbb.a $(UNIVALUE) $(LIBBTC) $(HIDAPI_FIRMWARE) 


#check if we should build the dbb app
if ENABLE_DBB_APP
bin_PROGRAMS += dbb-app

dbb_app_CONFIG_INCLUDES=-I$(builddir)/config
dbb_app_SOURCES = \
  dbb_app.h \
  dbb_app.cpp \
  dbb_util.h \
  dbb_util.cpp \
  dbb_wallet.h \
  dbb_wallet.cpp \
  dbb_netthread.h \
  dbb_netthread.cpp \
  dbb_comserver.h \
  dbb_comserver.cpp \
  dbb_configdata.h \
  dbb_ca.h \
  dbb_ca.cpp

dbb_app_CPPFLAGS = $(AM_CPPFLAGS) $(QR_CFLAGS) $(DBB_INCLUDES)
dbb_app_CFLAGS =
dbb_app_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)
dbb_app_LDADD = libdbb.a libbpwalletclient.a $(LIBBTC) $(UNIVALUE) $(LIBCURL) $(HIDAPI)

if ENABLE_QT

QT_FORMS_UI = \
  qt/ui/overview.ui \
  qt/ui/paymentproposal.ui \
  qt/ui/backupdialog.ui \
  qt/ui/signconfirmationdialog.ui \
  qt/ui/getaddressdialog.ui \
  qt/ui/verificationdialog.ui \
  qt/ui/qrcodesequence.ui \
  qt/ui/modalview.ui \
  qt/ui/settingsdialog.ui
	
DBB_GUI_H = \
	qt/paymentproposal.h \
    qt/signconfirmationdialog.h \
    qt/getaddressdialog.h \
    qt/qrcodesequence.h \
    qt/verificationdialog.h \
    qt/modalview.h \
    qt/clickablelabel.h \
    qt/settingsdialog.h \
    qt/update.h

DBB_GUI_CPP = \
	qt/paymentproposal.cpp \
    qt/backupdialog.cpp \
    qt/signconfirmationdialog.cpp \
    qt/getaddressdialog.cpp \
    qt/qrcodesequence.cpp \
    qt/verificationdialog.cpp \
    qt/modalview.cpp \
    qt/clickablelabel.cpp \
    qt/settingsdialog.cpp \
    qt/update.cpp

QT_MOC_CPP = \
	qt/moc_dbb_gui.cpp \
	qt/moc_paymentproposal.cpp \
    qt/moc_backupdialog.cpp \
    qt/moc_signconfirmationdialog.cpp \
    qt/moc_getaddressdialog.cpp \
    qt/moc_qrcodesequence.cpp \
    qt/moc_verificationdialog.cpp \
    qt/moc_modalview.cpp \
    qt/moc_clickablelabel.cpp \
    qt/moc_settingsdialog.cpp \
    qt/moc_update.cpp

QT_MOC = \
  qt/dbb_gui.moc \
  qt/paymentproposal.moc \
  qt/backupdialog.moc \
  qt/singconfirmationdialog.moc \
  qt/getaddressdialog.moc \
  qt/qrcodesequence.moc \
  qt/verificationdialog.moc \
  qt/modalview.moc \
  qt/clickablelabel.moc \
  qt/settingsdialog.moc \
  qt/update.moc


if USE_MULTIMEDIA
DBB_GUI_H += qt/qrcodescanner.h \
  qt/quirc/lib/quirc_internal.h \
  qt/quirc/lib/quirc.h

DBB_GUI_CPP += qt/qrcodescanner.cpp \
  qt/quirc/lib/decode.c \
  qt/quirc/lib/identify.c \
  qt/quirc/lib/quirc.c \
  qt/quirc/lib/version_db.c

QT_MOC_CPP += qt/moc_qrcodescanner.cpp
QT_MOC += qt/qrcodescanner.moc
endif

QT_FORMS_H=$(join $(dir $(QT_FORMS_UI)),$(addprefix ui_, $(notdir $(QT_FORMS_UI:.ui=.h))))

$(QT_MOC): $(QT_FORMS_H)
$(dbb_app_OBJECTS) $(dbb_app_OBJECTS) : | $(QT_MOC)

dbb_app_SOURCES += qt/dbb_gui.h $(DBB_GUI_H) qt/dbb_gui.cpp qt/qrc_dbbgui.cpp $(QT_MOC_CPP) $(QT_MOC) $(QT_FORMS_UI) $(DBB_GUI_CPP)

if TARGET_WINDOWS
dbb_app_SOURCES += qt/res/dbb-app-res.rc
endif

dbb_app_CPPFLAGS += $(QT_INCLUDES) $(QT_DBUS_INCLUDES) $(QT_MULTIMEDIA_INCLUDES) $(QT_MULTIMEDIAWIDGETS_INCLUDES) -I$(srcdir)/qt
dbb_app_CXXFLAGS = $(AM_CXXFLAGS) $(QT_PIE_FLAGS)
##dbb_app_LDFLAGS += -pthread $(QT_LDFLAGS) -pthread
dbb_app_LDFLAGS += $(RELDFLAGS) $(AM_LDFLAGS) $(QT_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)
dbb_app_LDADD += $(QT_LIBS) $(QT_DBUS_LIBS) $(QT_MULTIMEDIA_LIBS) $(QT_MULTIMEDIAWIDGETS_LIBS) $(QR_LIBS) $(SSL_LIBS)  $(CRYPTO_LIBS) -lcurl $(LINUX_LIBS)
dbb_app_LIBTOOLFLAGS = --tag CXX

CLEAN_QT = $(QT_MOC_CPP) $(QT_FORMS_H)

CLEANFILES = $(CLEAN_QT)

dbb_app_clean: FORCE
	rm -f $(CLEAN_QT) dbb-app$(EXEEXT)

clean-local:
	-$(MAKE) -C libbtc clean
	-$(MAKE) -C univalue clean
	-$(MAKE) -C secp256k1 clean
	-$(MAKE) -C hidapi clean

ui_%.h: %.ui
	@test -f $(UIC)
	@$(MKDIR_P) $(@D)
	$(UIC) -o $@ $< || (echo "Error creating $@"; false)

qt/qrc_%.cpp: qt/%.qrc
	$(RCC) -o $@ $< || (echo "Error creating $@"; false)

qt/moc_%.cpp: qt/%.h
	$(MOC) -o $@ $< || (echo "Error creating $@"; false)

.rc.o:
	@test -f $(WINDRES)
	$(AM_V_GEN) $(WINDRES) -DWINDRES_PREPROC -i $< -o $@

endif

endif
