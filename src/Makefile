# $KmKId: makefile,v 1.40 2021-01-10 20:20:19+00 kentd Exp $

XOPTS_WIN = -Wall -fomit-frame-pointer -march=pentium

SWIFTOBJS = AppDelegate.o MainView.o

include vars
include ldvars

.SUFFIXES: .dep .proto

AS = $(CC)

XLIBS = -L/usr/X11/lib
PERL = perl

all: $(TARGET)

specials:

specials_clean:


# Mac builds:
kegsmac: $(OBJECTS) compile_time.o $(SWIFTOBJS)
	clang $(CCOPTS) $(LDOPTS) $(OBJECTS) $(SWIFTOBJS) compile_time.o \
		$(LDFLAGS) -o kegsmac $(EXTRA_LIBS) \
		-isysroot /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk \
		-Xlinker -rpath -Xlinker @executable_path/../Frameworks \
		-Xlinker -rpath -Xlinker /usr/lib/swift \
		-Xlinker -no_deduplicate -fobjc-link-runtime \
		-L/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/swift/macosx \
		-L/usr/lib/swift
	mkdir -p ../KEGSMAC.app/Contents/Resources/Base.lproj/
	mkdir -p ../KEGSMAC.app/Contents/MacOS
	mkdir -p ../KEGSMAC.app/Contents/Frameworks
	PERL cp_kegs_libs kegsmac /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/swift/macosx ../KEGSMAC.app/Contents/Frameworks
	mv kegsmac ../KEGSMAC.app/Contents/MacOS/KEGSMAC
	echo "APPL????" > ../KEGSMAC.app/Contents/PkgInfo
	cp -f Info.plist ../KEGSMAC.app/Contents/
	cp -f $(PROJROOT)/lib/MainMenu.nib ../KEGSMAC.app/Contents/Resources/Base.lproj/
	#cp -f $(PROJROOT)/lib/kegsicon.icns ../KEGSMAC.app/Contents/Resources/
	#cp -f $(PROJROOT)/lib/2mg.icns ../KEGSMAC.app/Contents/Resources/
	#cp -f $(PROJROOT)/lib/525.icns ../KEGSMAC.app/Contents/Resources/
	#touch '../KEGSMAC.app/Icon?'

# Linux for X builds:
xkegs: $(OBJECTS) compile_time.o
	$(CC) $(CCOPTS) $(LDOPTS) $(OBJECTS) compile_time.o $(LDFLAGS) -o $(NAME)$(SUFFIX) $(XLIBS) $(EXTRA_LIBS) -lX11 -lXext
	mv xkegs ..

# Cygwin for X builds:
kegs.exe: $(OBJECTS) compile_time.o
	$(CC) $(CCOPTS) $(LDOPTS) $(OBJECTS) compile_time.o $(LDFLAGS) -o $(NAME)$(SUFFIX) $(XLIBS) $(EXTRA_LIBS) -lXext -lX11 -lm
	mv kegs.exe ..

# Mingw32 (native windows) builds: (broken, doesn't work currently)
kegswin.exe: $(OBJECTS) compile_time.o
	$(CC) $(CCOPTS) $(LDOPTS) $(OBJECTS) compile_time.o $(LDFLAGS) -o $(NAME)$(SUFFIX) $(EXTRA_LIBS) -lwinmm -lgdi32 -ldsound -lcomctl32 -lws2_32
	mv $(NAME)$(SUFFIX) ..


.s.o:
	$(AS) -c $(OPTS) -I. $*.s

.c.o:
	$(CC) $(CCOPTS) $(XOPTS) -c $(OPTS) -I. $*.c

.m.o:
	$(CC) $(CCOPTS) $(XOPTS) -c $(OPTS) -I. $*.m

AppDelegate.o: AppDelegate.swift
	sh ./comp_swift -c $(OPTS) -I. -primary-file $*.swift \
		MainView.swift -o $*.o

MainView.o: MainView.swift
	sh ./comp_swift -c $(OPTS) -I. -primary-file $*.swift \
		AppDelegate.swift -o $*.o

win32.o: win32.rc
	windres -o win32.o win32.rc

partls: partls.c
	cc $(CCOPTS) $(XOPTS) $(OPTS) -o partls partls.c

to_pro: prodos.h prodos_protos.h to_pro.c
	cc $(CCOPTS) $(XOPTS) $(OPTS) -o to_pro to_pro.c


.c.proto:
	$(KMKROOT)/bin/kmkproto $(XOPTS) $*.c > $*.proto
	echo >> $*.proto

.m.proto:
	$(KMKROOT)/bin/kmkproto $(XOPTS) $*.m > $*.proto
	echo >> $*.proto

$(PROTO_OUT_base): $(PROTO_FILE_LIST_base) ldvars
	$(KMKROOT)/bin/kmkproto_head $(PROTO_OUT_base) tmp_protos.h
	cat /dev/null $(PROTO_FILE_LIST_base) >> tmp_protos.h
	rm -f $(PROTO_OUT_base)
	mv tmp_protos.h $(PROTO_OUT_base)
	cp -f $(PROTO_OUT_base) ..

$(PROTO_OUT_xdriver): $(PROTO_FILE_LIST_xdriver) ldvars
	$(KMKROOT)/bin/kmkproto_head $(PROTO_OUT_xdriver) tmp_protos.h
	cat /dev/null $(PROTO_FILE_LIST_xdriver) >> tmp_protos.h
	rm -f $(PROTO_OUT_xdriver)
	mv tmp_protos.h $(PROTO_OUT_xdriver)
	cp -f $(PROTO_OUT_xdriver) ..

$(PROTO_OUT_macsnd_driver): $(PROTO_FILE_LIST_macsnd_driver) ldvars
	$(KMKROOT)/bin/kmkproto_head $(PROTO_OUT_macsnd_driver) tmp_protos.h
	cat /dev/null $(PROTO_FILE_LIST_macsnd_driver) >> tmp_protos.h
	rm -f $(PROTO_OUT_macsnd_driver)
	mv tmp_protos.h $(PROTO_OUT_macsnd_driver)
	cp -f $(PROTO_OUT_macsnd_driver) ..

$(PROTO_OUT_pulseaudio_driver): $(PROTO_FILE_LIST_pulseaudio_driver) ldvars
	$(KMKROOT)/bin/kmkproto_head $(PROTO_OUT_pulseaudio_driver) tmp_protos.h
	cat /dev/null $(PROTO_FILE_LIST_pulseaudio_driver) >> tmp_protos.h
	rm -f $(PROTO_OUT_pulseaudio_driver)
	mv tmp_protos.h $(PROTO_OUT_pulseaudio_driver)
	cp -f $(PROTO_OUT_pulseaudio_driver) ..


compile_time.o: $(OBJECTS)

include dependency

