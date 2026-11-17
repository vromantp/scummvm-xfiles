MODULE := engines/xfiles

MODULE_OBJS = \
	ui/captions.o \
	features/computers.o \
	debug/console.o \
	ui/cursors.o \
	ui/dialogbox.o \
	media/discfiles.o \
	features/headcredits.o \
	hot.o \
	ui/interface.o \
	ui/itemtext.o \
	ui/menu.o \
	metaengine.o \
	media/movie.o \
	media/neoaccess.o \
	features/phone.o \
	features/pda.o \
	media/pictures.o \
	ui/prefs.o \
	debug/refrun.o \
	saveload.o \
	script/script.o \
	script/script_dialog.o \
	script/script_hooks.o \
	script/script_media.o \
	script/script_save.o \
	script/script_view.o \
	features/shootout.o \
	media/sound.o \
	features/subliminal.o \
	features/tools.o \
	features/tools_view.o \
	script/vcdata.o \
	debug/vcdump.o \
	xfiles.o \
	xfiles_input.o \
	xfiles_media.o \
	xfiles_view.o

# This module can be built as a plugin
ifeq ($(ENABLE_XFILES), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
