#######################################################################################
#
# Compile-time options, to be modified by end user.
# To activate an option, just change to XXXX=ON except for the AUDIO option,
# which reads AUDIO=YYYY with YYYY=ALSA or YYYY=PULSE.
#
#######################################################################################
GPIO=
MIDI=
SATURN=
USBOZY=
SOAPYSDR=
STEMLAB=
EXTENDED_NR=
SERVER=
AUDIO=

#
# Explanation of compile time options
#
# GPIO         | If ON, compile with GPIO support (RaspPi only)
# MIDI         | If ON, compile with MIDI support
# SATURN       | If ON, compile with native SATURN/G2 XDMA support
# USBOZY       | If ON, piHPSDR can talk to legacy USB OZY radios
# SOAPYSDR     | If ON, piHPSDR can talk to radios via SoapySDR library
# STEMLAB      | If ON, piHPSDR can start SDR app on RedPitay via Web interface
# EXTENDED_NR  | If ON, piHPSDR can use extended noise reduction (VU3RDD WDSP version)
# SERVER       | If ON, include client/server code (still far from being complete)
# AUDIO        | If AUDIO=ALSA, use ALSA rather than PulseAudio on Linux

#######################################################################################
#
# No end-user changes below this line!
#
#######################################################################################

# get the OS Name
UNAME_S := $(shell uname -s)

# Get git commit version and date
GIT_DATE := $(firstword $(shell git --no-pager show --date=short --format="%ai" --name-only))
GIT_VERSION := $(shell git describe --abbrev=0 --tags --always --dirty)
GIT_COMMIT := $(shell git log --pretty=format:"%h"  -1)

#CFLAGS?= -O0 -g -Wno-deprecated-declarations -Wall -Wextra -Werror -Wno-error=unused-parameter -Wno-error=unused-function -Wno-error=unused-variable -Wno-error=implicit-fallthrough
CFLAGS?= -O3 -g -Wno-deprecated-declarations -Wall
#CFLAGS?= -O3 -Wno-deprecated-declarations -Wall
LINK?=   $(CC) 

#
# The "official" way to compile+link with pthreads is now to use the -pthread option
# *both* for the compile and the link step.
#
CFLAGS+=-pthread -I./src
LINK+=-pthread

PKG_CONFIG = pkg-config

WDSP_INCLUDE=-I./wdsp
WDSP_LIBS=wdsp/libwdsp.a `$(PKG_CONFIG) --libs fftw3`

##############################################################################
#
# Settings for optional features, to be requested by un-commenting lines above
#
##############################################################################

##############################################################################
#
# disable GPIO and SATURN for MacOS, simply because it is not there
#
##############################################################################

ifeq ($(UNAME_S), Darwin)
GPIO=
SATURN=
endif

##############################################################################
#
# Add modules for MIDI if requested.
# Note these are different for Linux/MacOS
#
##############################################################################

ifeq ($(MIDI),ON)
MIDI_OPTIONS=-D MIDI
MIDI_HEADERS= midi.h midi_menu.h alsa_midi.h
ifeq ($(UNAME_S), Darwin)
MIDI_SOURCES= src/mac_midi.c src/midi2.c src/midi3.c src/midi_menu.c
MIDI_OBJS= src/mac_midi.o src/midi2.o src/midi3.o src/midi_menu.o
MIDI_LIBS= -framework CoreMIDI -framework Foundation
endif
ifeq ($(UNAME_S), Linux)
MIDI_SOURCES= src/alsa_midi.c src/midi2.c src/midi3.c src/midi_menu.c
MIDI_OBJS= src/alsa_midi.o src/midi2.o src/midi3.o src/midi_menu.o
MIDI_LIBS= -lasound
endif
endif

##############################################################################
#
# Add libraries for Saturn support, if requested
#
##############################################################################

ifeq ($(SATURN),ON)
SATURN_OPTIONS=-D SATURN
SATURN_SOURCES= \
src/saturndrivers.c \
src/saturnregisters.c \
src/saturnserver.c \
src/saturnmain.c \
src/saturn_menu.c
SATURN_HEADERS= \
src/saturndrivers.h \
src/saturnregisters.h \
src/saturnserver.h \
src/saturnmain.h \
src/saturn_menu.h
SATURN_OBJS= \
src/saturndrivers.o \
src/saturnregisters.o \
src/saturnserver.o \
src/saturnmain.o \
src/saturn_menu.o
endif

##############################################################################
#
# Add libraries for USB OZY support, if requested
#
##############################################################################

ifeq ($(USBOZY),ON)
USBOZY_OPTIONS=-D USBOZY
USBOZY_LIBS=-lusb-1.0
USBOZY_SOURCES= \
src/ozyio.c
USBOZY_HEADERS= \
src/ozyio.h
USBOZY_OBJS= \
src/ozyio.o
endif

##############################################################################
#
# Add libraries for SoapySDR support, if requested
#
##############################################################################

ifeq ($(SOAPYSDR),ON)
SOAPYSDR_OPTIONS=-D SOAPYSDR
SOAPYSDRLIBS=-lSoapySDR
SOAPYSDR_SOURCES= \
src/soapy_discovery.c \
src/soapy_protocol.c
SOAPYSDR_HEADERS= \
src/soapy_discovery.h \
src/soapy_protocol.h
SOAPYSDR_OBJS= \
src/soapy_discovery.o \
src/soapy_protocol.o
endif

##############################################################################
#
# Add support for extended noise reduction, if requested
# This implies that one compiles against a wdsp.h e.g. in /usr/local/include,
# and links with a WDSP shared lib e.g. in /usr/local/lib
#
##############################################################################

ifeq ($(EXTENDED_NR), ON)
EXTNR_OPTIONS=-DEXTNR
WDSP_INCLUDE=
WDSP_LIBS=-lwdsp
endif

##############################################################################
#
# Add libraries for GPIO support, if requested
#
##############################################################################

ifeq ($(GPIO),ON)
GPIO_OPTIONS=-D GPIO
GPIOD_VERSION=$(shell pkg-config --modversion libgpiod)
ifeq ($(GPIOD_VERSION),1.2)
GPIO_OPTIONS += -D OLD_GPIOD
endif
GPIO_LIBS=-lgpiod -li2c
endif

##############################################################################
#
# Activate code for RedPitaya (Stemlab/Hamlab/plain vanilla), if requested
# This code detects the RedPitaya by its WWW interface and starts the SDR
# application.
# If the RedPitaya auto-starts the SDR application upon system start,
# this option is not needed!
#
##############################################################################

ifeq ($(STEMLAB), ON)
STEMLAB_OPTIONS=-D STEMLAB_DISCOVERY
STEMLAB_INCLUDE=`$(PKG_CONFIG) --cflags libcurl`
STEMLAB_LIBS=`$(PKG_CONFIG) --libs libcurl`
STEMLAB_SOURCES=src/stemlab_discovery.c
STEMLAB_HEADERS=src/stemlab_discovery.h
STEMLAB_OBJS=src/stemlab_discovery.o
endif

##############################################################################
#
# Activate code for remote operation, if requested.
# This feature is not yet finished. If finished, it
# allows to run two instances of piHPSDR on two
# different computers, one interacting with the operator
# and the other talking to the radio, and both computers
# may be connected by a long-distance internet connection.
#
##############################################################################

ifeq ($(SERVER), ON)
SERVER_OPTIONS=-D CLIENT_SERVER
SERVER_SOURCES= \
src/client_server.c src/server_menu.c
SERVER_HEADERS= \
src/client_server.h src/server_menu.h
SERVER_OBJS= \
src/client_server.o src/server_menu.o
endif

##############################################################################
#
# Options for audio module
#  - MacOS: only PORTAUDIO
#  - Linux: either PULSEAUDIO (default) or ALSA (upon request)
#
##############################################################################

ifeq ($(OS),Windows_NT)
  AUDIO=PORTAUDIO
else
  ifeq ($(UNAME_S), Darwin)
    AUDIO=PORTAUDIO
  endif
  ifeq ($(UNAME_S), Linux)
    ifneq ($(AUDIO), ALSA)
      AUDIO=PULSE
    endif
  endif
endif

##############################################################################
#
# Add libraries for using PulseAudio, if requested
#
##############################################################################

ifeq ($(AUDIO), PULSE)
AUDIO_OPTIONS=-DPULSEAUDIO
AUDIO_INCLUDE=
ifeq ($(UNAME_S), Linux)
  AUDIO_LIBS=-lpulse-simple -lpulse -lpulse-mainloop-glib
endif
ifeq ($(UNAME_S), Darwin)
  AUDIO_LIBS=-lpulse-simple -lpulse
endif
AUDIO_SOURCES=src/pulseaudio.c
AUDIO_OBJS=src/pulseaudio.o
endif

##############################################################################
#
# Add libraries for using ALSA, if requested
#
##############################################################################

ifeq ($(AUDIO), ALSA)
AUDIO_OPTIONS=-DALSA
AUDIO_INCLUDE=
AUDIO_LIBS=-lasound
AUDIO_SOURCES=src/audio.c
AUDIO_OBJS=src/audio.o
endif

##############################################################################
#
# Add libraries for using PortAudio, if requested
#
##############################################################################

ifeq ($(AUDIO), PORTAUDIO)
AUDIO_OPTIONS=-DPORTAUDIO
AUDIO_INCLUDE=`$(PKG_CONFIG) --cflags portaudio-2.0`
AUDIO_LIBS=`$(PKG_CONFIG) --libs portaudio-2.0`
AUDIO_SOURCES=src/portaudio.c
AUDIO_OBJS=src/portaudio.o
endif

##############################################################################
#
# End of "libraries for optional features" section
#
##############################################################################

##############################################################################
#
# Includes and Libraries for the graphical user interface (GTK)
#
##############################################################################

GTKINCLUDE=`$(PKG_CONFIG) --cflags gtk4`
GTKLIBS=`$(PKG_CONFIG) --libs gtk4` -lsndfile

##############################################################################
#
# Specify additional OS-dependent system libraries
#
##############################################################################

ifeq ($(UNAME_S), Linux)
SYSLIBS=-lrt
endif

ifeq ($(UNAME_S), Darwin)
SYSLIBS=-framework IOKit
endif

ifeq ($(OS),Windows_NT)
SYSLIBS=-lws2_32 -liphlpapi -lavrt
endif

##############################################################################
#
# All the command-line options to compile the *.c files
#
##############################################################################

OPTIONS=$(MIDI_OPTIONS) $(USBOZY_OPTIONS) \
	$(GPIO_OPTIONS) $(SOAPYSDR_OPTIONS) \
	$(ANDROMEDA_OPTIONS) \
	$(SATURN_OPTIONS) \
	$(STEMLAB_OPTIONS) \
	$(SERVER_OPTIONS) \
	$(AUDIO_OPTIONS) $(EXTNR_OPTIONS)\
	-D GIT_DATE='"$(GIT_DATE)"' -D GIT_VERSION='"$(GIT_VERSION)"' -D GIT_COMMIT='"$(GIT_COMMIT)"'

INCLUDES=$(GTKINCLUDE) $(WDSP_INCLUDE) $(AUDIO_INCLUDE) $(STEMLAB_INCLUDE)
COMPILE=$(CC) $(CFLAGS) $(OPTIONS) $(INCLUDES)

.c.o:
	$(COMPILE) -c -o $@ $<

##############################################################################
#
# All the libraries we need to link with (including WDSP, libm, $(SYSLIBS))
#
##############################################################################

LIBS=	$(LDFLAGS) $(AUDIO_LIBS) $(USBOZY_LIBS) $(GTKLIBS) $(GPIO_LIBS) $(SOAPYSDRLIBS) $(STEMLAB_LIBS) \
	$(MIDI_LIBS) $(WDSP_LIBS) -lm $(SYSLIBS)

##############################################################################
#
# The main target, the pihpsdr program
#
##############################################################################

PROGRAM=pihpsdr

##############################################################################
#
# The core *.c files in alphabetical order
#
##############################################################################

SOURCES= \
src/MacOS.c \
src/about_menu.c \
src/actions.c \
src/action_dialog.c \
src/agc_menu.c \
src/ant_menu.c \
src/appearance.c \
src/band.c \
src/band_menu.c \
src/bandstack_menu.c \
src/css.c \
src/configure.c \
src/cw_menu.c \
src/discovered.c \
src/discovery.c \
src/display_menu.c \
src/diversity_menu.c \
src/effects.c \
src/encoder_menu.c \
src/equalizer_menu.c \
src/exit_menu.c \
src/ext.c \
src/fft_menu.c \
src/filter.c \
src/filter_menu.c \
src/gpio.c \
src/i2c.c \
src/iambic.c \
src/led.c \
src/main.c \
src/message.c \
src/meter.c \
src/meter_menu.c \
src/mode.c \
src/mode_menu.c \
src/mystring.c \
src/new_discovery.c \
src/new_menu.c \
src/new_protocol.c \
src/noise_menu.c \
src/oc_menu.c \
src/old_discovery.c \
src/old_protocol.c \
src/pa_menu.c \
src/playcapture.c \
src/property.c \
src/protocols.c \
src/ps_menu.c \
src/radio.c \
src/radio_menu.c \
src/receiver.c \
src/rigctl.c \
src/rigctl_menu.c \
src/rx_menu.c \
src/rx_panadapter.c \
src/screen_menu.c \
src/sintab.c \
src/sliders.c \
src/startup.c \
src/store.c \
src/store_menu.c \
src/switch_menu.c \
src/toolbar.c \
src/toolbar_menu.c \
src/transmitter.c \
src/tx_menu.c \
src/tx_panadapter.c \
src/version.c \
src/vfo.c \
src/vfo_menu.c \
src/vox.c \
src/vox_menu.c \
src/waterfall.c \
src/wave.c \
src/xvtr_menu.c \
src/zoompan.c

##############################################################################
#
# The core *.h (header) files in alphabetical order
#
##############################################################################

HEADERS= \
src/MacOS.h \
src/about_menu.h \
src/actions.h \
src/action_dialog.h \
src/adc.h \
src/agc.h \
src/agc_menu.h \
src/alex.h \
src/ant_menu.h \
src/appearance.h \
src/band.h \
src/band_menu.h \
src/bandstack_menu.h \
src/bandstack.h \
src/channel.h \
src/configure.h \
src/css.h \
src/cw_menu.h \
src/dac.h \
src/discovered.h \
src/discovery.h \
src/display_menu.h \
src/diversity_menu.h \
src/effects.h \
src/encoder_menu.h \
src/equalizer_menu.h \
src/exit_menu.h \
src/ext.h \
src/fft_menu.h \
src/filter.h \
src/filter_menu.h \
src/gpio.h \
src/iambic.h \
src/i2c.h \
src/led.h \
src/main.h \
src/message.h \
src/meter.h \
src/meter_menu.h \
src/mode.h \
src/mode_menu.h \
src/mystring.h \
src/new_discovery.h \
src/new_menu.h \
src/new_protocol.h \
src/noise_menu.h \
src/oc_menu.h \
src/old_discovery.h \
src/old_protocol.h \
src/pa_menu.h \
src/playcapture.h \
src/property.h \
src/protocols.h \
src/ps_menu.h \
src/radio.h \
src/radio_menu.h \
src/receiver.h \
src/rigctl.h \
src/rigctl_menu.h \
src/rx_menu.h \
src/rx_panadapter.h \
src/screen_menu.h \
src/sintab.h \
src/sliders.h \
src/startup.h \
src/store.h \
src/store_menu.h \
src/switch_menu.h \
src/toolbar.h \
src/toolbar_menu.h \
src/transmitter.h \
src/tx_menu.h \
src/tx_panadapter.h \
src/version.h \
src/vfo.h \
src/vfo_menu.h \
src/vox.h \
src/vox_menu.h \
src/waterfall.h \
src/wave.h \
src/xvtr_menu.h \
src/zoompan.h

##############################################################################
#
# The core *.o (object) files in alphabetical order
#
##############################################################################

OBJS= \
src/MacOS.o \
src/about_menu.o \
src/actions.o \
src/action_dialog.o \
src/agc_menu.o \
src/ant_menu.o \
src/appearance.o \
src/band.o \
src/band_menu.o \
src/bandstack_menu.o \
src/configure.o \
src/css.o \
src/cw_menu.o \
src/discovered.o \
src/discovery.o \
src/display_menu.o \
src/diversity_menu.o \
src/effects.o \
src/encoder_menu.o \
src/equalizer_menu.o \
src/exit_menu.o \
src/ext.o \
src/fft_menu.o \
src/filter.o \
src/filter_menu.o \
src/gpio.o \
src/iambic.o \
src/i2c.o \
src/led.o \
src/main.o \
src/message.o \
src/meter.o \
src/meter_menu.o \
src/mode.o \
src/mode_menu.o \
src/mystring.o \
src/new_discovery.o \
src/new_menu.o \
src/new_protocol.o \
src/noise_menu.o \
src/oc_menu.o \
src/old_discovery.o \
src/old_protocol.o \
src/pa_menu.o \
src/playcapture.o \
src/property.o \
src/protocols.o \
src/ps_menu.o \
src/radio.o \
src/radio_menu.o \
src/receiver.o \
src/rigctl.o \
src/rigctl_menu.o \
src/rx_menu.o \
src/rx_panadapter.o \
src/screen_menu.o \
src/sintab.o \
src/sliders.o \
src/startup.o \
src/store.o \
src/store_menu.o \
src/switch_menu.o \
src/toolbar.o \
src/toolbar_menu.o \
src/transmitter.o \
src/tx_menu.o \
src/tx_panadapter.o \
src/version.o \
src/vfo.o \
src/vfo_menu.o \
src/vox.o \
src/vox_menu.o \
src/xvtr_menu.o \
src/waterfall.o \
src/wave.o \
src/zoompan.o

##############################################################################
#
# How to link the program
#
##############################################################################

$(PROGRAM):  $(OBJS) $(AUDIO_OBJS) $(USBOZY_OBJS) $(SOAPYSDR_OBJS) \
		$(MIDI_OBJS) $(STEMLAB_OBJS) $(SERVER_OBJS) $(SATURN_OBJS)
	$(COMPILE) -c -o src/version.o src/version.c
ifneq (z$(WDSP_INCLUDE), z)
	@+make -C wdsp
endif
	$(LINK) -o $(PROGRAM) $(OBJS) $(AUDIO_OBJS) $(USBOZY_OBJS) $(SOAPYSDR_OBJS) \
		$(MIDI_OBJS) $(STEMLAB_OBJS) $(SERVER_OBJS) $(SATURN_OBJS) $(LIBS)

##############################################################################
#
# "make check" invokes the cppcheck program to do a source-code checking.
#
# The "-pthread" compiler option is not valid for cppcheck and must be filtered out.
# Furthermore, we can add additional options to cppcheck in the variable CPPOPTIONS
#
# Normally cppcheck complains about variables that could be declared "const".
# Suppress this warning for callback functions because adding "const" would need
# an API change in many cases.
#
# On MacOS, cppcheck usually cannot find the system include files so we suppress any
# warnings therefrom, as well as warnings for functions defined in some
# library but never called.
# Furthermore, we can use --check-level=exhaustive on MacOS
# since there we have new newest version (2.11), while on RaspPi we still have
# older versions.
#
##############################################################################

CPPINCLUDES:=$(shell echo $(INCLUDES) | sed -e "s/-pthread / /" )

CPPOPTIONS= --inline-suppr --enable=all --suppress=unmatchedSuppression

ifeq ($(UNAME_S), Darwin)
CPPOPTIONS += -D__APPLE__
CPPOPTIONS += --check-level=exhaustive
CPPOPTIONS += --suppress=missingIncludeSystem
CPPOPTIONS += --suppress=unusedFunction
else
CPPOPTIONS += -D__linux__
endif

.PHONY:	cppcheck
cppcheck:
	cppcheck $(CPPOPTIONS) $(OPTIONS) $(CPPINCLUDES) $(AUDIO_SOURCES) $(SOURCES) \
	$(USBOZY_SOURCES)  $(SOAPYSDR_SOURCES) $(MIDI_SOURCES) $(STEMLAB_SOURCES) \
	$(SERVER_SOURCES) $(SATURN_SOURCES)

.PHONY:	clean
clean:
	rm -f src/*.o
	rm -f $(PROGRAM) hpsdrsim bootloader
	rm -rf $(PROGRAM).app
	@make -C release/LatexManual clean
	@make -C wdsp clean

#############################################################################
#
# hpsdrsim is a cool program that emulates an SDR board with UDP and TCP
# facilities. It even feeds back the TX signal and distorts it, so that
# you can test PureSignal.
# This feature only works if the sample rate is 48000
#
#############################################################################

src/hpsdrsim.o:     src/hpsdrsim.c  src/hpsdrsim.h
	$(CC) -c $(CFLAGS) -o src/hpsdrsim.o src/hpsdrsim.c
	
src/newhpsdrsim.o:	src/newhpsdrsim.c src/hpsdrsim.h
	$(CC) -c $(CFLAGS) -o src/newhpsdrsim.o src/newhpsdrsim.c

hpsdrsim:       src/hpsdrsim.o src/newhpsdrsim.o
	$(LINK) -o hpsdrsim src/hpsdrsim.o src/newhpsdrsim.o -lm


#############################################################################
#
# bootloader is a small command-line program that allows to
# set the radio's IP address and upload firmware through the
# ancient protocol. This program can only be run as root since
# this protocol requires "sniffing" at the Ethernet adapter
# (this "sniffing" is done via the pcap library)
#
#############################################################################

bootloader:	src/bootloader.c
	$(CC) -o bootloader src/bootloader.c -lpcap

#############################################################################
#
# Re-create the manual PDF from the manual LaTeX sources. This creates
# the PDF version of the manual in release/LaTexManual and DOES NOT over-
# write the manual in release.
# The PDF file in "release" is meant to be updated only once a year or so,
# because including frequently changing binaries in a git repository tends
# to blow up this repository. Instead, binaries should be re-created from
# source code files.
#
#############################################################################

#############################################################################
#
# Create a file named DEPEND containing dependencies, to be added to
# the Makefile. This is done here because we need lots of #defines
# to make it right.
#
#############################################################################

.PHONY: DEPEND
DEPEND:
	rm -f DEPEND
	touch DEPEND
	makedepend -DMIDI -DSATURN -DUSBOZY -DSOAPYSDR -DEXTNR -DGPIO \
		-DSTEMLAB_DISCOVERY -DCLIENT_SERVER -DPULSEAUDIO \
		-DPORTAUDIO -DALSA -D__APPLE__ -D__linux__ \
		-f DEPEND -I./src src/*.c src/*.h
#############################################################################
#
# This is for MacOS "app" creation ONLY
#
#       The piHPSDR working directory is
#	$HOME -> Application Support -> piHPSDR
#
#       That is the directory where the WDSP wisdom file (created upon first
#       start of piHPSDR) but also the radio settings and the midi.props file
#       are stored.
#
#       No libraries are included in the app bundle, so it will only run
#       on the computer where it was created, and on other computers which
#       have all librariesand possibly the SoapySDR support
#       modules installed.
#############################################################################

.PHONY: app
app:	$(OBJS) $(AUDIO_OBJS) $(USBOZY_OBJS)  $(SOAPYSDR_OBJS) \
		$(MIDI_OBJS) $(STEMLAB_OBJS) $(SERVER_OBJS) $(SATURN_OBJS)
ifneq (z$(WDSP_INCLUDE), z)
	@+make -C wdsp
endif
	$(LINK) -headerpad_max_install_names -o $(PROGRAM) $(OBJS) $(AUDIO_OBJS) $(USBOZY_OBJS)  \
		$(SOAPYSDR_OBJS) $(MIDI_OBJS) $(STEMLAB_OBJS) $(SERVER_OBJS) $(SATURN_OBJS) \
		$(LIBS) $(LDFLAGS)
	@rm -rf pihpsdr.app
	@mkdir -p pihpsdr.app/Contents/MacOS
	@mkdir -p pihpsdr.app/Contents/Frameworks
	@mkdir -p pihpsdr.app/Contents/Resources
	@cp pihpsdr pihpsdr.app/Contents/MacOS/pihpsdr
	@cp MacOS/PkgInfo pihpsdr.app/Contents
	@cp MacOS/Info.plist pihpsdr.app/Contents
	@cp MacOS/hpsdr.icns pihpsdr.app/Contents/Resources/hpsdr.icns
	@cp MacOS/hpsdr.png pihpsdr.app/Contents/Resources

#############################################################################
#
# What follows is automatically generated by the "makedepend" program
# implemented here with "make DEPEND". This should be re-done each time
# a header file is added, or added to a C source code file.
#
#############################################################################

# DO NOT DELETE

src/MacOS.o: src/message.h
src/about_menu.o: src/new_menu.h src/about_menu.h src/discovered.h
src/about_menu.o: src/radio.h src/adc.h src/dac.h src/receiver.h
src/about_menu.o: src/transmitter.h src/version.h src/mystring.h
src/action_dialog.o: src/main.h src/actions.h
src/actions.o: src/main.h src/discovery.h src/receiver.h src/sliders.h
src/actions.o: src/transmitter.h src/actions.h src/band_menu.h
src/actions.o: src/diversity_menu.h src/vfo.h src/mode.h src/radio.h
src/actions.o: src/adc.h src/dac.h src/discovered.h src/radio_menu.h
src/actions.o: src/new_menu.h src/new_protocol.h src/MacOS.h src/ps_menu.h
src/actions.o: src/agc.h src/filter.h src/band.h src/bandstack.h
src/actions.o: src/noise_menu.h src/client_server.h src/ext.h src/zoompan.h
src/actions.o: src/gpio.h src/toolbar.h src/iambic.h src/store.h
src/actions.o: src/message.h src/mystring.h
src/agc_menu.o: src/new_menu.h src/agc_menu.h src/agc.h src/band.h
src/agc_menu.o: src/bandstack.h src/radio.h src/adc.h src/dac.h
src/agc_menu.o: src/discovered.h src/receiver.h src/transmitter.h src/vfo.h
src/agc_menu.o: src/mode.h src/ext.h src/client_server.h
src/alsa_midi.o: src/actions.h src/midi.h src/midi_menu.h src/alsa_midi.h
src/alsa_midi.o: src/message.h
src/ant_menu.o: src/new_menu.h src/ant_menu.h src/band.h src/bandstack.h
src/ant_menu.o: src/radio.h src/adc.h src/dac.h src/discovered.h
src/ant_menu.o: src/receiver.h src/transmitter.h src/new_protocol.h
src/ant_menu.o: src/MacOS.h src/soapy_protocol.h src/message.h
src/appearance.o: src/appearance.h
src/audio.o: src/radio.h src/adc.h src/dac.h src/discovered.h src/receiver.h
src/audio.o: src/transmitter.h src/audio.h src/mode.h src/vfo.h src/message.h
src/band.o: src/bandstack.h src/band.h src/filter.h src/mode.h src/property.h
src/band.o: src/mystring.h src/radio.h src/adc.h src/dac.h src/discovered.h
src/band.o: src/receiver.h src/transmitter.h src/vfo.h
src/band_menu.o: src/new_menu.h src/band_menu.h src/band.h src/bandstack.h
src/band_menu.o: src/filter.h src/mode.h src/radio.h src/adc.h src/dac.h
src/band_menu.o: src/discovered.h src/receiver.h src/transmitter.h src/vfo.h
src/band_menu.o: src/client_server.h
src/bandstack_menu.o: src/new_menu.h src/bandstack_menu.h src/band.h
src/bandstack_menu.o: src/bandstack.h src/filter.h src/mode.h src/radio.h
src/bandstack_menu.o: src/adc.h src/dac.h src/discovered.h src/receiver.h
src/bandstack_menu.o: src/transmitter.h src/vfo.h
src/client_server.o: src/discovered.h src/adc.h src/dac.h src/receiver.h
src/client_server.o: src/transmitter.h src/radio.h src/main.h src/vfo.h
src/client_server.o: src/mode.h src/client_server.h src/ext.h src/audio.h
src/client_server.o: src/zoompan.h src/noise_menu.h src/radio_menu.h
src/client_server.o: src/sliders.h src/actions.h src/message.h src/mystring.h
src/configure.o: src/radio.h src/adc.h src/dac.h src/discovered.h
src/configure.o: src/receiver.h src/transmitter.h src/main.h src/channel.h
src/configure.o: src/actions.h src/gpio.h src/i2c.h src/message.h
src/css.o: src/css.h src/message.h
src/cw_menu.o: src/new_menu.h src/pa_menu.h src/band.h src/bandstack.h
src/cw_menu.o: src/filter.h src/mode.h src/radio.h src/adc.h src/dac.h
src/cw_menu.o: src/discovered.h src/receiver.h src/transmitter.h
src/cw_menu.o: src/new_protocol.h src/MacOS.h src/old_protocol.h src/iambic.h
src/cw_menu.o: src/ext.h src/client_server.h
src/discovered.o: src/discovered.h
src/discovery.o: src/discovered.h src/old_discovery.h src/new_discovery.h
src/discovery.o: src/soapy_discovery.h src/main.h src/radio.h src/adc.h
src/discovery.o: src/dac.h src/receiver.h src/transmitter.h src/ozyio.h
src/discovery.o: src/stemlab_discovery.h src/ext.h src/client_server.h
src/discovery.o: src/actions.h src/gpio.h src/configure.h src/protocols.h
src/discovery.o: src/property.h src/mystring.h src/message.h src/saturnmain.h
src/discovery.o: src/saturnregisters.h
src/display_menu.o: src/main.h src/new_menu.h src/display_menu.h src/radio.h
src/display_menu.o: src/adc.h src/dac.h src/discovered.h src/receiver.h
src/display_menu.o: src/transmitter.h
src/diversity_menu.o: src/new_menu.h src/diversity_menu.h src/radio.h
src/diversity_menu.o: src/adc.h src/dac.h src/discovered.h src/receiver.h
src/diversity_menu.o: src/transmitter.h src/new_protocol.h src/MacOS.h
src/diversity_menu.o: src/old_protocol.h src/sliders.h src/actions.h
src/diversity_menu.o: src/ext.h src/client_server.h
src/encoder_menu.o: src/main.h src/new_menu.h src/agc_menu.h src/agc.h
src/encoder_menu.o: src/band.h src/bandstack.h src/channel.h src/radio.h
src/encoder_menu.o: src/adc.h src/dac.h src/discovered.h src/receiver.h
src/encoder_menu.o: src/transmitter.h src/vfo.h src/mode.h src/actions.h
src/encoder_menu.o: src/action_dialog.h src/gpio.h src/i2c.h
src/equalizer_menu.o: src/new_menu.h src/equalizer_menu.h src/radio.h
src/equalizer_menu.o: src/adc.h src/dac.h src/discovered.h src/receiver.h
src/equalizer_menu.o: src/transmitter.h src/ext.h src/client_server.h
src/equalizer_menu.o: src/vfo.h src/mode.h src/message.h
src/exit_menu.o: src/main.h src/new_menu.h src/exit_menu.h src/discovery.h
src/exit_menu.o: src/radio.h src/adc.h src/dac.h src/discovered.h
src/exit_menu.o: src/receiver.h src/transmitter.h src/new_protocol.h
src/exit_menu.o: src/MacOS.h src/old_protocol.h src/soapy_protocol.h
src/exit_menu.o: src/actions.h src/gpio.h src/message.h src/saturnmain.h
src/exit_menu.o: src/saturnregisters.h
src/ext.o: src/main.h src/discovery.h src/receiver.h src/sliders.h
src/ext.o: src/transmitter.h src/actions.h src/toolbar.h src/gpio.h src/vfo.h
src/ext.o: src/mode.h src/radio.h src/adc.h src/dac.h src/discovered.h
src/ext.o: src/radio_menu.h src/new_menu.h src/noise_menu.h src/ext.h
src/ext.o: src/client_server.h src/zoompan.h src/equalizer_menu.h
src/fft_menu.o: src/new_menu.h src/fft_menu.h src/radio.h src/adc.h src/dac.h
src/fft_menu.o: src/discovered.h src/receiver.h src/transmitter.h
src/fft_menu.o: src/message.h
src/filter.o: src/sliders.h src/receiver.h src/transmitter.h src/actions.h
src/filter.o: src/filter.h src/mode.h src/vfo.h src/radio.h src/adc.h
src/filter.o: src/dac.h src/discovered.h src/property.h src/mystring.h
src/filter.o: src/message.h src/ext.h src/client_server.h
src/filter_menu.o: src/new_menu.h src/filter_menu.h src/band.h
src/filter_menu.o: src/bandstack.h src/filter.h src/mode.h src/radio.h
src/filter_menu.o: src/adc.h src/dac.h src/discovered.h src/receiver.h
src/filter_menu.o: src/transmitter.h src/vfo.h src/ext.h src/client_server.h
src/filter_menu.o: src/message.h
src/gpio.o: src/band.h src/bandstack.h src/channel.h src/discovered.h
src/gpio.o: src/mode.h src/filter.h src/toolbar.h src/gpio.h src/radio.h
src/gpio.o: src/adc.h src/dac.h src/receiver.h src/transmitter.h src/main.h
src/gpio.o: src/property.h src/mystring.h src/vfo.h src/new_menu.h
src/gpio.o: src/encoder_menu.h src/diversity_menu.h src/actions.h src/i2c.h
src/gpio.o: src/ext.h src/client_server.h src/sliders.h src/new_protocol.h
src/gpio.o: src/MacOS.h src/zoompan.h src/iambic.h src/message.h
src/hpsdrsim.o: src/MacOS.h src/hpsdrsim.h
src/i2c.o: src/i2c.h src/actions.h src/gpio.h src/band.h src/bandstack.h
src/i2c.o: src/band_menu.h src/radio.h src/adc.h src/dac.h src/discovered.h
src/i2c.o: src/receiver.h src/transmitter.h src/toolbar.h src/vfo.h
src/i2c.o: src/mode.h src/ext.h src/client_server.h src/message.h
src/iambic.o: src/gpio.h src/radio.h src/adc.h src/dac.h src/discovered.h
src/iambic.o: src/receiver.h src/transmitter.h src/new_protocol.h src/MacOS.h
src/iambic.o: src/iambic.h src/ext.h src/client_server.h src/mode.h src/vfo.h
src/iambic.o: src/message.h
src/led.o: src/message.h
src/mac_midi.o: src/discovered.h src/receiver.h src/transmitter.h src/adc.h
src/mac_midi.o: src/dac.h src/radio.h src/actions.h src/midi.h
src/mac_midi.o: src/midi_menu.h src/alsa_midi.h src/message.h
src/main.o: src/appearance.h src/audio.h src/receiver.h src/band.h
src/main.o: src/bandstack.h src/main.h src/discovered.h src/configure.h
src/main.o: src/actions.h src/gpio.h src/new_menu.h src/radio.h src/adc.h
src/main.o: src/dac.h src/transmitter.h src/version.h src/discovery.h
src/main.o: src/new_protocol.h src/MacOS.h src/old_protocol.h
src/main.o: src/soapy_protocol.h src/ext.h src/client_server.h src/vfo.h
src/main.o: src/mode.h src/css.h src/exit_menu.h src/message.h src/mystring.h
src/main.o: src/startup.h
src/meter.o: src/appearance.h src/band.h src/bandstack.h src/receiver.h
src/meter.o: src/meter.h src/radio.h src/adc.h src/dac.h src/discovered.h
src/meter.o: src/transmitter.h src/version.h src/mode.h src/vox.h
src/meter.o: src/new_menu.h src/vfo.h src/message.h
src/meter_menu.o: src/new_menu.h src/receiver.h src/meter_menu.h src/meter.h
src/meter_menu.o: src/radio.h src/adc.h src/dac.h src/discovered.h
src/meter_menu.o: src/transmitter.h
src/midi2.o: src/MacOS.h src/receiver.h src/discovered.h src/adc.h src/dac.h
src/midi2.o: src/transmitter.h src/radio.h src/main.h src/actions.h
src/midi2.o: src/midi.h src/alsa_midi.h src/message.h
src/midi3.o: src/actions.h src/message.h src/midi.h
src/midi_menu.o: src/main.h src/discovered.h src/mode.h src/filter.h
src/midi_menu.o: src/band.h src/bandstack.h src/receiver.h src/transmitter.h
src/midi_menu.o: src/adc.h src/dac.h src/radio.h src/actions.h
src/midi_menu.o: src/action_dialog.h src/midi.h src/alsa_midi.h
src/midi_menu.o: src/new_menu.h src/midi_menu.h src/property.h src/mystring.h
src/midi_menu.o: src/message.h
src/mode_menu.o: src/new_menu.h src/band_menu.h src/band.h src/bandstack.h
src/mode_menu.o: src/filter.h src/mode.h src/radio.h src/adc.h src/dac.h
src/mode_menu.o: src/discovered.h src/receiver.h src/transmitter.h src/vfo.h
src/new_discovery.o: src/discovered.h src/discovery.h src/message.h
src/new_discovery.o: src/mystring.h
src/new_menu.o: src/audio.h src/receiver.h src/new_menu.h src/about_menu.h
src/new_menu.o: src/exit_menu.h src/radio_menu.h src/rx_menu.h src/ant_menu.h
src/new_menu.o: src/display_menu.h src/pa_menu.h src/rigctl_menu.h
src/new_menu.o: src/oc_menu.h src/cw_menu.h src/store_menu.h src/xvtr_menu.h
src/new_menu.o: src/equalizer_menu.h src/radio.h src/adc.h src/dac.h
src/new_menu.o: src/discovered.h src/transmitter.h src/meter_menu.h
src/new_menu.o: src/band_menu.h src/bandstack_menu.h src/mode_menu.h
src/new_menu.o: src/filter_menu.h src/noise_menu.h src/agc_menu.h
src/new_menu.o: src/vox_menu.h src/diversity_menu.h src/tx_menu.h
src/new_menu.o: src/ps_menu.h src/encoder_menu.h src/switch_menu.h
src/new_menu.o: src/toolbar_menu.h src/vfo_menu.h src/fft_menu.h src/main.h
src/new_menu.o: src/actions.h src/gpio.h src/old_protocol.h
src/new_menu.o: src/new_protocol.h src/MacOS.h src/server_menu.h src/midi.h
src/new_menu.o: src/midi_menu.h src/screen_menu.h src/saturn_menu.h
src/new_protocol.o: src/alex.h src/audio.h src/receiver.h src/band.h
src/new_protocol.o: src/bandstack.h src/new_protocol.h src/MacOS.h
src/new_protocol.o: src/discovered.h src/mode.h src/filter.h src/radio.h
src/new_protocol.o: src/adc.h src/dac.h src/transmitter.h src/vfo.h
src/new_protocol.o: src/toolbar.h src/gpio.h src/vox.h src/ext.h
src/new_protocol.o: src/client_server.h src/iambic.h src/rigctl.h
src/new_protocol.o: src/message.h src/saturnmain.h src/saturnregisters.h
src/newhpsdrsim.o: src/MacOS.h src/hpsdrsim.h
src/noise_menu.o: src/new_menu.h src/noise_menu.h src/band.h src/bandstack.h
src/noise_menu.o: src/filter.h src/mode.h src/radio.h src/adc.h src/dac.h
src/noise_menu.o: src/discovered.h src/receiver.h src/transmitter.h src/vfo.h
src/noise_menu.o: src/ext.h src/client_server.h
src/oc_menu.o: src/main.h src/new_menu.h src/oc_menu.h src/band.h
src/oc_menu.o: src/bandstack.h src/filter.h src/mode.h src/radio.h src/adc.h
src/oc_menu.o: src/dac.h src/discovered.h src/receiver.h src/transmitter.h
src/oc_menu.o: src/new_protocol.h src/MacOS.h src/message.h
src/old_discovery.o: src/discovered.h src/discovery.h src/old_discovery.h
src/old_discovery.o: src/stemlab_discovery.h src/message.h src/mystring.h
src/old_protocol.o: src/MacOS.h src/audio.h src/receiver.h src/band.h
src/old_protocol.o: src/bandstack.h src/discovered.h src/mode.h src/filter.h
src/old_protocol.o: src/old_protocol.h src/radio.h src/adc.h src/dac.h
src/old_protocol.o: src/transmitter.h src/vfo.h src/ext.h src/client_server.h
src/old_protocol.o: src/iambic.h src/message.h src/ozyio.h
src/ozyio.o: src/ozyio.h src/message.h
src/pa_menu.o: src/new_menu.h src/pa_menu.h src/band.h src/bandstack.h
src/pa_menu.o: src/radio.h src/adc.h src/dac.h src/discovered.h
src/pa_menu.o: src/receiver.h src/transmitter.h src/vfo.h src/mode.h
src/pa_menu.o: src/message.h
src/portaudio.o: src/radio.h src/adc.h src/dac.h src/discovered.h
src/portaudio.o: src/receiver.h src/transmitter.h src/mode.h src/audio.h
src/portaudio.o: src/message.h src/vfo.h
src/property.o: src/property.h src/mystring.h src/message.h
src/protocols.o: src/radio.h src/adc.h src/dac.h src/discovered.h
src/protocols.o: src/receiver.h src/transmitter.h src/protocols.h
src/protocols.o: src/property.h src/mystring.h
src/ps_menu.o: src/new_menu.h src/radio.h src/adc.h src/dac.h
src/ps_menu.o: src/discovered.h src/receiver.h src/transmitter.h
src/ps_menu.o: src/toolbar.h src/gpio.h src/new_protocol.h src/MacOS.h
src/ps_menu.o: src/vfo.h src/mode.h src/ext.h src/client_server.h
src/ps_menu.o: src/message.h src/mystring.h
src/pulseaudio.o: src/radio.h src/adc.h src/dac.h src/discovered.h
src/pulseaudio.o: src/receiver.h src/transmitter.h src/audio.h src/mode.h
src/pulseaudio.o: src/vfo.h src/message.h
src/radio.o: src/appearance.h src/adc.h src/dac.h src/audio.h src/receiver.h
src/radio.o: src/discovered.h src/filter.h src/mode.h src/main.h src/radio.h
src/radio.o: src/transmitter.h src/agc.h src/band.h src/bandstack.h
src/radio.o: src/channel.h src/property.h src/mystring.h src/new_menu.h
src/radio.o: src/new_protocol.h src/MacOS.h src/old_protocol.h src/store.h
src/radio.o: src/soapy_protocol.h src/actions.h src/gpio.h src/vfo.h
src/radio.o: src/vox.h src/meter.h src/rx_panadapter.h src/tx_panadapter.h
src/radio.o: src/waterfall.h src/zoompan.h src/sliders.h src/toolbar.h
src/radio.o: src/rigctl.h src/ext.h src/client_server.h src/radio_menu.h
src/radio.o: src/iambic.h src/rigctl_menu.h src/screen_menu.h src/midi.h
src/radio.o: src/alsa_midi.h src/midi_menu.h src/message.h src/saturnmain.h
src/radio.o: src/saturnregisters.h src/saturnserver.h
src/radio_menu.o: src/main.h src/discovered.h src/new_menu.h src/radio_menu.h
src/radio_menu.o: src/adc.h src/band.h src/bandstack.h src/filter.h
src/radio_menu.o: src/mode.h src/radio.h src/dac.h src/receiver.h
src/radio_menu.o: src/transmitter.h src/sliders.h src/actions.h
src/radio_menu.o: src/new_protocol.h src/MacOS.h src/old_protocol.h
src/radio_menu.o: src/soapy_protocol.h src/gpio.h src/vfo.h src/ext.h
src/radio_menu.o: src/client_server.h
src/receiver.o: src/agc.h src/audio.h src/receiver.h src/band.h
src/receiver.o: src/bandstack.h src/channel.h src/discovered.h src/filter.h
src/receiver.o: src/mode.h src/main.h src/meter.h src/property.h
src/receiver.o: src/mystring.h src/radio.h src/adc.h src/dac.h
src/receiver.o: src/transmitter.h src/vfo.h src/rx_panadapter.h src/zoompan.h
src/receiver.o: src/sliders.h src/actions.h src/waterfall.h
src/receiver.o: src/new_protocol.h src/MacOS.h src/old_protocol.h
src/receiver.o: src/soapy_protocol.h src/ext.h src/client_server.h
src/receiver.o: src/new_menu.h src/message.h
src/rigctl.o: src/receiver.h src/toolbar.h src/gpio.h src/band_menu.h
src/rigctl.o: src/sliders.h src/transmitter.h src/actions.h src/rigctl.h
src/rigctl.o: src/radio.h src/adc.h src/dac.h src/discovered.h src/channel.h
src/rigctl.o: src/filter.h src/mode.h src/band.h src/bandstack.h
src/rigctl.o: src/filter_menu.h src/vfo.h src/agc.h src/store.h src/ext.h
src/rigctl.o: src/client_server.h src/rigctl_menu.h src/noise_menu.h
src/rigctl.o: src/new_protocol.h src/MacOS.h src/old_protocol.h src/iambic.h
src/rigctl.o: src/new_menu.h src/zoompan.h src/exit_menu.h src/message.h
src/rigctl.o: src/mystring.h
src/rigctl_menu.o: src/new_menu.h src/rigctl_menu.h src/rigctl.h src/band.h
src/rigctl_menu.o: src/bandstack.h src/radio.h src/adc.h src/dac.h
src/rigctl_menu.o: src/discovered.h src/receiver.h src/transmitter.h
src/rigctl_menu.o: src/vfo.h src/mode.h src/message.h src/mystring.h
src/rx_menu.o: src/audio.h src/receiver.h src/new_menu.h src/rx_menu.h
src/rx_menu.o: src/band.h src/bandstack.h src/discovered.h src/filter.h
src/rx_menu.o: src/mode.h src/radio.h src/adc.h src/dac.h src/transmitter.h
src/rx_menu.o: src/sliders.h src/actions.h src/new_protocol.h src/MacOS.h
src/rx_menu.o: src/message.h src/mystring.h
src/rx_panadapter.o: src/appearance.h src/agc.h src/band.h src/bandstack.h
src/rx_panadapter.o: src/discovered.h src/radio.h src/adc.h src/dac.h
src/rx_panadapter.o: src/receiver.h src/transmitter.h src/rx_panadapter.h
src/rx_panadapter.o: src/vfo.h src/mode.h src/actions.h src/gpio.h
src/rx_panadapter.o: src/client_server.h src/ozyio.h
src/saturn_menu.o: src/new_menu.h src/saturn_menu.h src/saturnserver.h
src/saturn_menu.o: src/radio.h src/adc.h src/dac.h src/discovered.h
src/saturn_menu.o: src/receiver.h src/transmitter.h
src/saturndrivers.o: src/saturndrivers.h src/saturnregisters.h src/message.h
src/saturnmain.o: src/saturnregisters.h src/saturndrivers.h src/saturnmain.h
src/saturnmain.o: src/saturnserver.h src/discovered.h src/new_protocol.h
src/saturnmain.o: src/MacOS.h src/receiver.h src/message.h src/mystring.h
src/saturnregisters.o: src/saturnregisters.h src/message.h
src/saturnserver.o: src/saturnregisters.h src/saturnserver.h
src/saturnserver.o: src/saturndrivers.h src/saturnmain.h src/message.h
src/screen_menu.o: src/radio.h src/adc.h src/dac.h src/discovered.h
src/screen_menu.o: src/receiver.h src/transmitter.h src/new_menu.h src/main.h
src/screen_menu.o: src/appearance.h src/message.h
src/server_menu.o: src/new_menu.h src/server_menu.h src/radio.h src/adc.h
src/server_menu.o: src/dac.h src/discovered.h src/receiver.h
src/server_menu.o: src/transmitter.h src/client_server.h
src/sliders.o: src/appearance.h src/receiver.h src/sliders.h
src/sliders.o: src/transmitter.h src/actions.h src/mode.h src/filter.h
src/sliders.o: src/bandstack.h src/band.h src/discovered.h src/new_protocol.h
src/sliders.o: src/MacOS.h src/soapy_protocol.h src/vfo.h src/agc.h
src/sliders.o: src/channel.h src/radio.h src/adc.h src/dac.h src/property.h
src/sliders.o: src/mystring.h src/main.h src/ext.h src/client_server.h
src/sliders.o: src/message.h
src/soapy_discovery.o: src/discovered.h src/soapy_discovery.h src/message.h
src/soapy_discovery.o: src/mystring.h
src/soapy_protocol.o: src/band.h src/bandstack.h src/channel.h
src/soapy_protocol.o: src/discovered.h src/mode.h src/filter.h src/receiver.h
src/soapy_protocol.o: src/transmitter.h src/radio.h src/adc.h src/dac.h
src/soapy_protocol.o: src/main.h src/soapy_protocol.h src/audio.h src/vfo.h
src/soapy_protocol.o: src/ext.h src/client_server.h src/message.h
src/startup.o: src/message.h src/mystring.h
src/stemlab_discovery.o: src/discovered.h src/discovery.h src/radio.h
src/stemlab_discovery.o: src/adc.h src/dac.h src/receiver.h src/transmitter.h
src/stemlab_discovery.o: src/message.h src/mystring.h
src/store.o: src/bandstack.h src/band.h src/filter.h src/mode.h
src/store.o: src/property.h src/mystring.h src/store.h src/store_menu.h
src/store.o: src/radio.h src/adc.h src/dac.h src/discovered.h src/receiver.h
src/store.o: src/transmitter.h src/ext.h src/client_server.h src/vfo.h
src/store.o: src/message.h
src/store_menu.o: src/radio.h src/adc.h src/dac.h src/discovered.h
src/store_menu.o: src/receiver.h src/transmitter.h src/new_menu.h
src/store_menu.o: src/store_menu.h src/store.h src/bandstack.h src/mode.h
src/store_menu.o: src/filter.h src/message.h
src/switch_menu.o: src/main.h src/new_menu.h src/agc_menu.h src/agc.h
src/switch_menu.o: src/band.h src/bandstack.h src/channel.h src/radio.h
src/switch_menu.o: src/adc.h src/dac.h src/discovered.h src/receiver.h
src/switch_menu.o: src/transmitter.h src/vfo.h src/mode.h src/toolbar.h
src/switch_menu.o: src/gpio.h src/actions.h src/action_dialog.h src/i2c.h
src/toolbar.o: src/actions.h src/gpio.h src/toolbar.h src/mode.h src/filter.h
src/toolbar.o: src/bandstack.h src/band.h src/discovered.h src/new_protocol.h
src/toolbar.o: src/MacOS.h src/receiver.h src/old_protocol.h src/vfo.h
src/toolbar.o: src/agc.h src/channel.h src/radio.h src/adc.h src/dac.h
src/toolbar.o: src/transmitter.h src/property.h src/mystring.h src/new_menu.h
src/toolbar.o: src/ext.h src/client_server.h src/message.h
src/toolbar_menu.o: src/radio.h src/adc.h src/dac.h src/discovered.h
src/toolbar_menu.o: src/receiver.h src/transmitter.h src/new_menu.h
src/toolbar_menu.o: src/actions.h src/action_dialog.h src/gpio.h
src/toolbar_menu.o: src/toolbar.h
src/transmitter.o: src/band.h src/bandstack.h src/channel.h src/main.h
src/transmitter.o: src/receiver.h src/meter.h src/filter.h src/mode.h
src/transmitter.o: src/property.h src/mystring.h src/radio.h src/adc.h
src/transmitter.o: src/dac.h src/discovered.h src/transmitter.h src/vfo.h
src/transmitter.o: src/vox.h src/toolbar.h src/gpio.h src/tx_panadapter.h
src/transmitter.o: src/waterfall.h src/new_protocol.h src/MacOS.h
src/transmitter.o: src/old_protocol.h src/ps_menu.h src/soapy_protocol.h
src/transmitter.o: src/audio.h src/ext.h src/client_server.h src/sliders.h
src/transmitter.o: src/actions.h src/ozyio.h src/sintab.h src/message.h
src/tx_menu.o: src/audio.h src/receiver.h src/new_menu.h src/radio.h
src/tx_menu.o: src/adc.h src/dac.h src/discovered.h src/transmitter.h
src/tx_menu.o: src/sliders.h src/actions.h src/ext.h src/client_server.h
src/tx_menu.o: src/filter.h src/mode.h src/vfo.h src/new_protocol.h
src/tx_menu.o: src/MacOS.h src/message.h src/mystring.h
src/tx_panadapter.o: src/appearance.h src/agc.h src/band.h src/bandstack.h
src/tx_panadapter.o: src/discovered.h src/radio.h src/adc.h src/dac.h
src/tx_panadapter.o: src/receiver.h src/transmitter.h src/rx_panadapter.h
src/tx_panadapter.o: src/tx_panadapter.h src/vfo.h src/mode.h src/actions.h
src/tx_panadapter.o: src/gpio.h src/ext.h src/client_server.h src/new_menu.h
src/tx_panadapter.o: src/message.h
src/vfo.o: src/appearance.h src/discovered.h src/main.h src/agc.h src/mode.h
src/vfo.o: src/filter.h src/bandstack.h src/band.h src/property.h
src/vfo.o: src/mystring.h src/radio.h src/adc.h src/dac.h src/receiver.h
src/vfo.o: src/transmitter.h src/new_protocol.h src/MacOS.h
src/vfo.o: src/soapy_protocol.h src/vfo.h src/channel.h src/toolbar.h
src/vfo.o: src/gpio.h src/new_menu.h src/rigctl.h src/client_server.h
src/vfo.o: src/ext.h src/actions.h src/noise_menu.h src/equalizer_menu.h
src/vfo.o: src/message.h
src/vfo_menu.o: src/new_menu.h src/band.h src/bandstack.h src/filter.h
src/vfo_menu.o: src/mode.h src/radio.h src/adc.h src/dac.h src/discovered.h
src/vfo_menu.o: src/receiver.h src/transmitter.h src/vfo.h src/ext.h
src/vfo_menu.o: src/client_server.h src/radio_menu.h
src/vox.o: src/radio.h src/adc.h src/dac.h src/discovered.h src/receiver.h
src/vox.o: src/transmitter.h src/vox.h src/vfo.h src/mode.h src/ext.h
src/vox.o: src/client_server.h
src/vox_menu.o: src/appearance.h src/led.h src/new_menu.h src/radio.h
src/vox_menu.o: src/adc.h src/dac.h src/discovered.h src/receiver.h
src/vox_menu.o: src/transmitter.h src/vfo.h src/mode.h src/vox_menu.h
src/vox_menu.o: src/vox.h src/ext.h src/client_server.h src/message.h
src/waterfall.o: src/radio.h src/adc.h src/dac.h src/discovered.h
src/waterfall.o: src/receiver.h src/transmitter.h src/vfo.h src/mode.h
src/waterfall.o: src/band.h src/bandstack.h src/waterfall.h
src/waterfall.o: src/client_server.h
src/xvtr_menu.o: src/new_menu.h src/band.h src/bandstack.h src/filter.h
src/xvtr_menu.o: src/mode.h src/xvtr_menu.h src/radio.h src/adc.h src/dac.h
src/xvtr_menu.o: src/discovered.h src/receiver.h src/transmitter.h src/vfo.h
src/xvtr_menu.o: src/message.h src/mystring.h
src/zoompan.o: src/appearance.h src/main.h src/receiver.h src/radio.h
src/zoompan.o: src/adc.h src/dac.h src/discovered.h src/transmitter.h
src/zoompan.o: src/vfo.h src/mode.h src/sliders.h src/actions.h src/zoompan.h
src/zoompan.o: src/client_server.h src/ext.h src/message.h
src/audio.o: src/receiver.h
src/band.o: src/bandstack.h
src/ext.o: src/client_server.h
src/filter.o: src/mode.h
src/new_protocol.o: src/MacOS.h src/receiver.h
src/property.o: src/mystring.h
src/radio.o: src/adc.h src/dac.h src/discovered.h src/receiver.h
src/radio.o: src/transmitter.h
src/saturndrivers.o: src/saturnregisters.h
src/saturnmain.o: src/saturnregisters.h
src/sliders.o: src/receiver.h src/transmitter.h src/actions.h
src/store.o: src/bandstack.h
src/toolbar.o: src/gpio.h
src/vfo.o: src/mode.h
