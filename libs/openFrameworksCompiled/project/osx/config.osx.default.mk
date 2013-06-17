

PLATFORM_SHARED_LIBRARY_EXTENSION:=dylib
PLATFORM_STATIC_LIBRARY_EXTENSION:=a

################################################################################
# PLATFORM SPECIFIC CHECKS
#   This is a platform defined section to create internal flags to enable or 
#   disable the addition of various features within this makefile.  For 
#   instance, on Linux, we check to see if there GTK+-2.0 is defined, allowing 
#   us to include that library and generate DEFINES that are interpreted as 
#   ifdefs within the openFrameworks core source code.
################################################################################

PLATFORM_PROJECT_DEBUG_BIN_NAME:=bin/$(APPNAME)_debug.app
PLATFORM_PROJECT_DEBUG_TARGET:=bin/$(APPNAME)_debug
PLATFORM_PROJECT_RELEASE_BIN_NAME:=bin/$(APPNAME).app
PLATFORM_PROJECT_RELEASE_TARGET:=bin/$(APPNAME)
PLATFORM_RUN_COMMAND:=open

##########################################################################################
# PLATFORM DEFINES
#   Create a list of DEFINES for this platform.  The list will be converted into 
#   CFLAGS with the "-D" flag later in the makefile.  An example of fully qualified flag
#   might look something like this: -DTARGET_OPENGLES2
#
#   DEFINES are used throughout the openFrameworks code, especially when making
#   #ifdef decisions for cross-platform compatibility.  For instance, when chosing a 
#   video playback framework, the openFrameworks base classes look at the DEFINES
#   to determine what source files to include or what default player to use.
#
# Note: Be sure to leave a leading space when using a += operator to add items to the list
##########################################################################################

PLATFORM_DEFINES:=__MACOSX_CORE__

##########################################################################################
# PLATFORM REQUIRED ADDON
#   This is a list of addons required for this platform.  This list is used to EXCLUDE
#   addon source files when compiling projects, while INCLUDING their header files.
#   During core library compilation, this is used to include required addon header files
#   as needed within the core. 
#
#   For instance, if you are compiling for Android, you would add ofxAndroid here.
#   If you are compiling for Raspberry Pi, you would add ofxRaspberryPi here.
#
# Note: Be sure to leave a leading space when using a += operator to add items to the list
##########################################################################################

PLATFORM_REQUIRED_ADDONS:= 

##########################################################################################
# PLATFORM CFLAGS
#   This is a list of fully qualified CFLAGS required when compiling for this platform.
#   These flags will always be added when compiling a project or the core library.  These
#   Flags are presented to the compiler AFTER the PLATFORM_OPTIMIZATION_CFLAGS below. 
#
# Note: Be sure to leave a leading space when using a += operator to add items to the list
##########################################################################################

# Warning Flags (http://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html)
PLATFORM_CFLAGS := -Wall
PLATFORM_CFLAGS += -Wno-deprecated-declarations
PLATFORM_CFLAGS += -Wno-c++11-extensions
PLATFORM_CFLAGS += -Wno-null-conversion
PLATFORM_CFLAGS += -Wno-unused-variable
PLATFORM_CFLAGS += -Wno-unused-comparison

# Code Generation Option Flags (http://gcc.gnu.org/onlinedocs/gcc/Code-Gen-Options.html)
PLATFORM_CFLAGS += -fexceptions

MAC_OS_XCODE_ROOT:=$(shell xcode-select -print-path)

# search to see if we are using a new version of 
ifeq ($(findstring .app, $(MAC_OS_XCODE_ROOT)),.app)
    MAC_OS_SDK_PATH:=\
        $(MAC_OS_XCODE_ROOT)/Platforms/MacOSX.platform/Developer/SDKs
else
    MAC_OS_SDK_PATH:=\
        $(MAC_OS_XCODE_ROOT)/SDKs
endif

#ifeq ($(wildcard /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer),/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer)
#	MAC_OS_SDK_PATH=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs
#else
#    MAC_OS_SDK_PATH=/Developer/SDKs
#endif

ifndef MAC_OS_SDK
    ifeq ($(wildcard $(MAC_OS_SDK_PATH)/MacOSX10.8.sdk),\
            $(MAC_OS_SDK_PATH)/MacOSX10.8.sdk)
        MAC_OS_SDK=10.8
    else ifeq ($(wildcard $(MAC_OS_SDK_PATH)/MacOSX10.7.sdk),\
            $(MAC_OS_SDK_PATH)/MacOSX10.7.sdk)
        MAC_OS_SDK=10.7
    else ifeq ($(wildcard $(MAC_OS_SDK_PATH)/MacOSX10.6.sdk),$(MAC_OS_SDK_PATH)/MacOSX10.6.sdk)
        MAC_OS_SDK=10.6
    else
        $(error MAC_OS_SDK cannot be determined.  Please check your config.osx.default.mk file)
    endif
endif

MAC_OS_SDK_ROOT = $(MAC_OS_SDK_PATH)/MacOSX$(MAC_OS_SDK).sdk

# Architecture / Machine Flags (http://gcc.gnu.org/onlinedocs/gcc/Submodel-Options.html)
ifeq ($(shell \
        gcc -march=native -S -o /dev/null -xc /dev/null 2> /dev/null; \
        echo $$?\
        ),\
        0\
    )
    PLATFORM_CFLAGS += -march=native
    PLATFORM_CFLAGS += -mtune=native
endif

# Optimization options (http://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html)
PLATFORM_CFLAGS += -finline-functions
#PLATFORM_CFLAGS += -funroll-all-loops
PLATFORM_CFLAGS += -Os
PLATFORM_CFLAGS += -arch i386
# other osx
PLATFORM_CFLAGS += -fpascal-strings

PLATFORM_CFLAGS += -isysroot $(MAC_OS_SDK_ROOT)
# TODO: can we put this in the PLATFORM_FRAMEWORK_SEARCH_PATHS
#PLATFORM_CFLAGS += -F$(MAC_OS_SDK_ROOT)/System/Library/Frameworks
PLATFORM_CFLAGS += -mmacosx-version-min=$(MAC_OS_SDK)

PLATFORM_CFLAGS += -fasm-blocks 
PLATFORM_CFLAGS += -funroll-loops 
PLATFORM_CFLAGS += -mssse3
PLATFORM_CFLAGS += -fmessage-length=0

ifeq ($(MAC_OS_SDK),10.6)
PLATFORM_CFLAGS += -pipe 
PLATFORM_CFLAGS += -Wno-trigraphs 
PLATFORM_CFLAGS += -fasm-blocks 
PLATFORM_CFLAGS += -Wno-deprecated-declarations 
PLATFORM_CFLAGS += -Wno-invalid-offsetof 
PLATFORM_CFLAGS += -gdwarf-2
endif

PLATFORM_CFLAGS += -x objective-c++

################################################################################
# PLATFORM LDFLAGS
#   This is a list of fully qualified LDFLAGS required when linking for this 
#   platform. These flags will always be added when linking a project.
#
#   Note: Leave a leading space when adding list items with the += operator
################################################################################

PLATFORM_LDFLAGS :=
PLATFORM_LDFLAGS += -arch i386
# TODO: can we put this in the PLATFORM_FRAMEWORK_SEARCH_PATHS
# PLATFORM_LDFLAGS += -F$(PATH_OF_LIBS)/glut/lib/osx/
PLATFORM_LDFLAGS += -mmacosx-version-min=$(MAC_OS_SDK)

################################################################################
# PLATFORM OPTIMIZATION CFLAGS
#   These are lists of CFLAGS that are target-specific.  While any flags could 
#   be conditionally added, they are usually limited to optimization flags.  
#   These flags are added BEFORE the PLATFORM_CFLAGS.
#
#   PLATFORM_OPTIMIZATION_CFLAGS_RELEASE flags are only applied to 
#      RELEASE targets.
#
#   PLATFORM_OPTIMIZATION_CFLAGS_DEBUG flags are only applied to 
#      DEBUG targets.
#
#   Note: Leave a leading space when adding list items with the += operator
################################################################################

# RELEASE Debugging options (http://gcc.gnu.org/onlinedocs/gcc/Debugging-Options.html)
PLATFORM_OPTIMIZATION_CFLAGS_RELEASE :=

# DEBUG Debugging options (http://gcc.gnu.org/onlinedocs/gcc/Debugging-Options.html)
PLATFORM_OPTIMIZATION_CFLAGS_DEBUG := -g3

################################################################################
# PLATFORM EXCLUSIONS
#   During compilation, these makefiles will generate lists of sources, headers 
#   and third party libraries to be compiled and linked into a program or core 
#   library. The PLATFORM_EXCLUSIONS is a list of fully qualified file 
#   paths that will be used to exclude matching paths and files during list 
#   generation.
#
#   Each item in the PLATFORM_EXCLUSIONS list will be treated as a complete
#   string unless the user adds a wildcard (%) operator to match subdirectories.
#   GNU make only allows one wildcard for matching.  The second wildcard (%) is
#   treated literally.
#
#   Note: Leave a leading space when adding list items with the += operator
################################################################################

# erase all
PLATFORM_EXCLUSIONS =

# core sources
PLATFORM_EXCLUSIONS += $(PATH_OF_LIBS)/openFrameworks/video/ofDirectShowGrabber.cpp
PLATFORM_EXCLUSIONS += $(PATH_OF_LIBS)/openFrameworks/video/ofGstUtils.cpp
PLATFORM_EXCLUSIONS += $(PATH_OF_LIBS)/openFrameworks/video/ofGstVideoGrabber.cpp
PLATFORM_EXCLUSIONS += $(PATH_OF_LIBS)/openFrameworks/video/ofGstVideoPlayer.cpp
PLATFORM_EXCLUSIONS += $(PATH_OF_LIBS)/openFrameworks/app/ofAppEGLWindow.cpp

# third party
PLATFORM_EXCLUSIONS += $(PATH_OF_LIBS)/poco/include/Poco
PLATFORM_EXCLUSIONS += $(PATH_OF_LIBS)/poco/include/CppUnit
PLATFORM_EXCLUSIONS += $(PATH_OF_LIBS)/poco/include/Poco/%
PLATFORM_EXCLUSIONS += $(PATH_OF_LIBS)/poco/include/CppUnit/%
PLATFORM_EXCLUSIONS += $(PATH_OF_LIBS)/videoInput/%
PLATFORM_EXCLUSIONS += $(PATH_OF_LIBS)/quicktime/%

# third party static libs (this may not matter due to exclusions in poco's libsorder.make)
PLATFORM_EXCLUSIONS += $(PATH_OF_LIBS)/poco/lib/$(PLATFORM_LIB_SUBPATH)/libPocoCrypto.a
PLATFORM_EXCLUSIONS += $(PATH_OF_LIBS)/poco/lib/$(PLATFORM_LIB_SUBPATH)/libPocoData.a
PLATFORM_EXCLUSIONS += $(PATH_OF_LIBS)/poco/lib/$(PLATFORM_LIB_SUBPATH)/libPocoDataMySQL.a
PLATFORM_EXCLUSIONS += $(PATH_OF_LIBS)/poco/lib/$(PLATFORM_LIB_SUBPATH)/libPocoDataODBC.a
PLATFORM_EXCLUSIONS += $(PATH_OF_LIBS)/poco/lib/$(PLATFORM_LIB_SUBPATH)/libPocoDataSQLite.a
PLATFORM_EXCLUSIONS += $(PATH_OF_LIBS)/poco/lib/$(PLATFORM_LIB_SUBPATH)/libPocoNetSSL.a
PLATFORM_EXCLUSIONS += $(PATH_OF_LIBS)/poco/lib/$(PLATFORM_LIB_SUBPATH)/libPocoZip.a

################################################################################
# PLATFORM HEADER SEARCH PATHS
#   These are header search paths that are platform specific and are specified 
#   using fully-qualified paths.  The include flag (i.e. -I) is prefixed 
#   automatically. These are usually not required, but may be required by some 
#   experimental platforms such as the raspberry pi or other other embedded 
#   architectures.
#
#   Note: Leave a leading space when adding list items with the += operator
################################################################################

PLATFORM_HEADER_SEARCH_PATHS =

################################################################################
# PLATFORM SOURCES SEARCH PATHS
#   These are header search paths that are platform specific and are specified 
#   using fully-qualified paths.  The include flag (i.e. -I) is prefixed 
#   automatically. These are usually not required, but may be required by some 
#   experimental platforms such as the raspberry pi or other other embedded 
#   architectures.
#
#   Note: Leave a leading space when adding list items with the += operator
################################################################################

PLATFORM_SOURCES =


################################################################################
# PLATFORM LIBRARIES
#   These are library names/paths that are platform specific and are specified 
#   using names or paths.  The library flag (i.e. -l) is prefixed automatically.
#
#   PLATFORM_LIBRARIES are libraries that can be found in the library search 
#       paths.
#   PLATFORM_STATIC_LIBRARIES is a list of required static libraries.
#   PLATFORM_SHARED_LIBRARIES is a list of required shared libraries.
#   PLATFORM_PKG_CONFIG_LIBRARIES is a list of required libraries that are 
#       under system control and are easily accesible via the package 
#       configuration utility (i.e. pkg-config)
#
#   See the helpfile for the -l flag here for more information:
#       http://gcc.gnu.org/onlinedocs/gcc/Link-Options.html
#
#   Note: Leave a leading space when adding list items with the += operator
################################################################################

PLATFORM_LIBRARIES =
ifneq ($(MAC_OS_SDK),10.6)
    PLATFORM_LIBRARIES += objc
endif

#static libraries (fully qualified paths)
PLATFORM_STATIC_LIBRARIES =

# shared libraries 
PLATFORM_SHARED_LIBRARIES =

################################################################################
# PLATFORM LIBRARY SEARCH PATHS
#   These are library search paths that are platform specific and are specified 
#   using fully-qualified paths.  The lib search flag (i.e. -L) is prefixed 
#   automatically. The -L paths are used to find libraries defined above with 
#   the -l flag.
#
#   See the the following link for more information on the -L flag:
#       http://gcc.gnu.org/onlinedocs/gcc/Directory-Options.html 
#
#   Note: Leave a leading space when adding list items with the += operator
################################################################################

PLATFORM_LIBRARY_SEARCH_PATHS =

################################################################################
# PLATFORM FRAMEWORKS
#   These are a list of platform frameworks.  
#   These are used exclusively with Darwin/OSX.
#
#   Note: Leave a leading space when adding list items with the += operator
################################################################################

PLATFORM_FRAMEWORKS := 
PLATFORM_FRAMEWORKS += Accelerate
PLATFORM_FRAMEWORKS += QTKit
PLATFORM_FRAMEWORKS += GLUT
PLATFORM_FRAMEWORKS += AGL
PLATFORM_FRAMEWORKS += ApplicationServices
PLATFORM_FRAMEWORKS += AudioToolbox
PLATFORM_FRAMEWORKS += Carbon
PLATFORM_FRAMEWORKS += CoreAudio
PLATFORM_FRAMEWORKS += CoreFoundation
PLATFORM_FRAMEWORKS += CoreServices
PLATFORM_FRAMEWORKS += OpenGL
PLATFORM_FRAMEWORKS += QuickTime

ifneq ($(MAC_OS_SDK),10.6)
    PLATFORM_FRAMEWORKS += CoreVideo
    PLATFORM_FRAMEWORKS += Cocoa
endif

################################################################################
# PLATFORM FRAMEWORK SEARCH PATHS
#   These are a list of platform framework search paths.  
#   These are used exclusively with Darwin/OSX.
#
#   Note: Leave a leading space when adding list items with the += operator
################################################################################

PLATFORM_FRAMEWORK_SEARCH_PATHS := 
PLATFORM_FRAMEWORK_SEARCH_PATHS += /System/Library/Frameworks
PLATFORM_FRAMEWORK_SEARCH_PATHS += $(MAC_OS_SDK_ROOT)/System/Library/Frameworks
PLATFORM_FRAMEWORK_SEARCH_PATHS += $(PATH_OF_LIBS)/glut/lib/osx/

################################################################################
# LOW LEVEL CONFIGURATION BELOW
#   The following sections should only rarely be modified.  They are meant for 
#   developers who need fine control when, for instance, creating a platform 
#   specific makefile for a new openFrameworks platform, such as raspberry pi. 
################################################################################

################################################################################
# PLATFORM CONFIGURATIONS
# These will override the architecture vars generated by configure.platform.mk
################################################################################

# we force i386 here, because currently we do not compile 64 bit binaries
PLATFORM_ARCH = i386

#PLATFORM_OS =
#PLATFORM_LIBS_PATH =

################################################################################
# PLATFORM CXX
#    Don't want to use a default compiler?
################################################################################
#PLATFORM_CXX=

afterplatform: $(TARGET_NAME)
	@rm -rf $(BIN_NAME)
	@mkdir -p $(BIN_NAME)
	@mkdir -p $(BIN_NAME)/Contents
	@mkdir -p $(BIN_NAME)/Contents/Frameworks
	@mkdir -p $(BIN_NAME)/Contents/MacOS
	@mkdir -p $(BIN_NAME)/Contents/Resources
	
	# TODO: look for an existing Info.plist to copy to $(BIN_NAME)/Contents
	# TODO: look for an existing icon bundle to copy to $(BIN_NAME)/Contents

	@echo '<?xml version="1.0" encoding="UTF-8"?>' > $(BIN_NAME)/Contents/Info.plist
	@echo '!DOCTYPE plist PUBLIC "-//Apple Computer//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' >> $(BIN_NAME)/Contents/Info.plist
	@echo '<plist version="1.0">' >> $(BIN_NAME)/Contents/Info.plist
	@echo '<dict>' >> $(BIN_NAME)/Contents/Info.plist
	@echo '  <key>CFBundleGetInfoString</key>' >> $(BIN_NAME)/Contents/Info.plist
	@echo '  <string>$(BIN_NAME)</string>' >> $(BIN_NAME)/Contents/Info.plist
	@echo '  <key>CFBundleExecutable</key>' >> $(BIN_NAME)/Contents/Info.plist
	@echo '  <string>$(BIN_NAME)</string>' >> $(BIN_NAME)/Contents/Info.plist
	@echo '  <key>CFBundleIdentifier</key>' >> $(BIN_NAME)/Contents/Info.plist
	@echo '  <string>com.your-company-name.www</string>' >> $(BIN_NAME)/Contents/Info.plist
	@echo '  <key>CFBundleName</key>' >> $(BIN_NAME)/Contents/Info.plist
	@echo '  <string>$(BIN_NAME)</string>' >> $(BIN_NAME)/Contents/Info.plist
	#@echo '  <key>CFBundleIconFile</key>' >> $(BIN_NAME)/Contents/Info.plist
	#@echo '  <string>foo.icns</string>' >> $(BIN_NAME)/Contents/Info.plist
	@echo '  <key>CFBundleShortVersionString</key>' >> $(BIN_NAME)/Contents/Info.plist
	@echo '  <string>0.01</string>' >> $(BIN_NAME)/Contents/Info.plist
	@echo '  <key>CFBundleInfoDictionaryVersion</key>' >> $(BIN_NAME)/Contents/Info.plist
	@echo '  <string>6.0</string>' >> $(BIN_NAME)/Contents/Info.plist
	@echo '  <key>CFBundlePackageType</key>' >> $(BIN_NAME)/Contents/Info.plist
	@echo '  <string>APPL</string>' >> $(BIN_NAME)/Contents/Info.plist
	@echo '  <key>IFMajorVersion</key>' >> $(BIN_NAME)/Contents/Info.plist
	@echo '  <integer>0</integer>' >> $(BIN_NAME)/Contents/Info.plist
	@echo '  <key>IFMinorVersion</key>' >> $(BIN_NAME)/Contents/Info.plist
	@echo '  <integer>1</integer>' >> $(BIN_NAME)/Contents/Info.plist
	@echo '</dict>' >> $(BIN_NAME)/Contents/Info.plist
	@echo '</plist>' >> $(BIN_NAME)/Contents/Info.plist
	
	@echo TARGET=$(TARGET)
	
	# libfmodex ... blah
	@install_name_tool -change ./libfmodex.dylib @executable_path/libfmodex.dylib $(TARGET)
	@cp $(PATH_OF_LIBS)/fmodex/lib/$(ABI_LIB_SUBPATH)/* $(BIN_NAME)/Contents/MacOS
	
	# frameworks to be copied
	@echo $(OF_PROJECT_FRAMEWORKS_EXPORTS)
	# aaa

	@cp -r $(PATH_OF_LIBS)/glut/lib/$(ABI_LIB_SUBPATH)/* $(BIN_NAME)/Contents/Frameworks

	# move the target executable into the bundle
	@mv $(TARGET) $(BIN_NAME)/Contents/MacOS
	
	@echo
	@echo "     compiling done"
	@echo "     to launch the application"
	@echo
	@echo "     open $(BIN_NAME)"
	@echo "     "
	@echo "     - or -"
	@echo "     "
	@echo "     make $(RUN_TARGET)"
	@echo
	