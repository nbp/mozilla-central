# This file is generated by gyp; do not edit.

TOOLSET := target
TARGET := webrtc_video_coding
DEFS_Debug := '-D_FILE_OFFSET_BITS=64' \
	'-DCHROMIUM_BUILD' \
	'-DUSE_NSS=1' \
	'-DTOOLKIT_USES_GTK=1' \
	'-DGTK_DISABLE_SINGLE_INCLUDES=1' \
	'-DENABLE_REMOTING=1' \
	'-DENABLE_P2P_APIS=1' \
	'-DENABLE_CONFIGURATION_POLICY' \
	'-DENABLE_INPUT_SPEECH' \
	'-DENABLE_NOTIFICATIONS' \
	'-DENABLE_GPU=1' \
	'-DENABLE_EGLIMAGE=1' \
	'-DUSE_SKIA=1' \
	'-DENABLE_REGISTER_PROTOCOL_HANDLER=1' \
	'-DENABLE_WEB_INTENTS=1' \
	'-DENABLE_PLUGIN_INSTALLATION=1' \
	'-DWEBRTC_TARGET_PC' \
	'-DWEBRTC_LINUX' \
	'-DWEBRTC_THREAD_RR' \
	'-D__STDC_FORMAT_MACROS' \
	'-DDYNAMIC_ANNOTATIONS_ENABLED=1' \
	'-DWTF_USE_DYNAMIC_ANNOTATIONS=1' \
	'-D_DEBUG'

# Flags passed to all source files.
CFLAGS_Debug := -Werror \
	-pthread \
	-fno-exceptions \
	-fno-strict-aliasing \
	-Wall \
	-Wno-unused-parameter \
	-Wno-missing-field-initializers \
	-fvisibility=hidden \
	-pipe \
	-fPIC \
	-Wextra \
	-Wno-unused-parameter \
	-Wno-missing-field-initializers \
	-O0 \
	-g

# Flags passed to only C files.
CFLAGS_C_Debug := 

# Flags passed to only C++ files.
CFLAGS_CC_Debug := -fno-rtti \
	-fno-threadsafe-statics \
	-fvisibility-inlines-hidden \
	-Wsign-compare

INCS_Debug := -Isrc \
	-I. \
	-Isrc/modules/video_coding/main/interface \
	-Isrc/modules/interface \
	-Isrc/modules/video_coding/codecs/interface \
	-Isrc/common_video/interface \
	-Isrc/modules/video_coding/codecs/i420/main/interface \
	-Isrc/modules/video_coding/codecs/vp8/main/interface \
	-Isrc/common_video/interface \
	-Isrc/modules/video_coding/codecs/interface \
	-Isrc/system_wrappers/interface

DEFS_Release := '-D_FILE_OFFSET_BITS=64' \
	'-DCHROMIUM_BUILD' \
	'-DUSE_NSS=1' \
	'-DTOOLKIT_USES_GTK=1' \
	'-DGTK_DISABLE_SINGLE_INCLUDES=1' \
	'-DENABLE_REMOTING=1' \
	'-DENABLE_P2P_APIS=1' \
	'-DENABLE_CONFIGURATION_POLICY' \
	'-DENABLE_INPUT_SPEECH' \
	'-DENABLE_NOTIFICATIONS' \
	'-DENABLE_GPU=1' \
	'-DENABLE_EGLIMAGE=1' \
	'-DUSE_SKIA=1' \
	'-DENABLE_REGISTER_PROTOCOL_HANDLER=1' \
	'-DENABLE_WEB_INTENTS=1' \
	'-DENABLE_PLUGIN_INSTALLATION=1' \
	'-DWEBRTC_TARGET_PC' \
	'-DWEBRTC_LINUX' \
	'-DWEBRTC_THREAD_RR' \
	'-D__STDC_FORMAT_MACROS' \
	'-DNDEBUG' \
	'-DNVALGRIND' \
	'-DDYNAMIC_ANNOTATIONS_ENABLED=0'

# Flags passed to all source files.
CFLAGS_Release := -Werror \
	-pthread \
	-fno-exceptions \
	-fno-strict-aliasing \
	-Wall \
	-Wno-unused-parameter \
	-Wno-missing-field-initializers \
	-fvisibility=hidden \
	-pipe \
	-fPIC \
	-Wextra \
	-Wno-unused-parameter \
	-Wno-missing-field-initializers \
	-O2 \
	-fno-ident \
	-fdata-sections \
	-ffunction-sections

# Flags passed to only C files.
CFLAGS_C_Release := 

# Flags passed to only C++ files.
CFLAGS_CC_Release := -fno-rtti \
	-fno-threadsafe-statics \
	-fvisibility-inlines-hidden \
	-Wsign-compare

INCS_Release := -Isrc \
	-I. \
	-Isrc/modules/video_coding/main/interface \
	-Isrc/modules/interface \
	-Isrc/modules/video_coding/codecs/interface \
	-Isrc/common_video/interface \
	-Isrc/modules/video_coding/codecs/i420/main/interface \
	-Isrc/modules/video_coding/codecs/vp8/main/interface \
	-Isrc/common_video/interface \
	-Isrc/modules/video_coding/codecs/interface \
	-Isrc/system_wrappers/interface

OBJS := $(obj).target/$(TARGET)/src/modules/video_coding/main/source/codec_database.o \
	$(obj).target/$(TARGET)/src/modules/video_coding/main/source/codec_timer.o \
	$(obj).target/$(TARGET)/src/modules/video_coding/main/source/content_metrics_processing.o \
	$(obj).target/$(TARGET)/src/modules/video_coding/main/source/decoding_state.o \
	$(obj).target/$(TARGET)/src/modules/video_coding/main/source/encoded_frame.o \
	$(obj).target/$(TARGET)/src/modules/video_coding/main/source/exp_filter.o \
	$(obj).target/$(TARGET)/src/modules/video_coding/main/source/frame_buffer.o \
	$(obj).target/$(TARGET)/src/modules/video_coding/main/source/frame_dropper.o \
	$(obj).target/$(TARGET)/src/modules/video_coding/main/source/frame_list.o \
	$(obj).target/$(TARGET)/src/modules/video_coding/main/source/generic_decoder.o \
	$(obj).target/$(TARGET)/src/modules/video_coding/main/source/generic_encoder.o \
	$(obj).target/$(TARGET)/src/modules/video_coding/main/source/inter_frame_delay.o \
	$(obj).target/$(TARGET)/src/modules/video_coding/main/source/jitter_buffer.o \
	$(obj).target/$(TARGET)/src/modules/video_coding/main/source/jitter_buffer_common.o \
	$(obj).target/$(TARGET)/src/modules/video_coding/main/source/jitter_estimator.o \
	$(obj).target/$(TARGET)/src/modules/video_coding/main/source/media_opt_util.o \
	$(obj).target/$(TARGET)/src/modules/video_coding/main/source/media_optimization.o \
	$(obj).target/$(TARGET)/src/modules/video_coding/main/source/packet.o \
	$(obj).target/$(TARGET)/src/modules/video_coding/main/source/qm_select.o \
	$(obj).target/$(TARGET)/src/modules/video_coding/main/source/receiver.o \
	$(obj).target/$(TARGET)/src/modules/video_coding/main/source/rtt_filter.o \
	$(obj).target/$(TARGET)/src/modules/video_coding/main/source/session_info.o \
	$(obj).target/$(TARGET)/src/modules/video_coding/main/source/timestamp_extrapolator.o \
	$(obj).target/$(TARGET)/src/modules/video_coding/main/source/timestamp_map.o \
	$(obj).target/$(TARGET)/src/modules/video_coding/main/source/timing.o \
	$(obj).target/$(TARGET)/src/modules/video_coding/main/source/video_coding_impl.o

# Add to the list of files we specially track dependencies for.
all_deps += $(OBJS)

# CFLAGS et al overrides must be target-local.
# See "Target-specific Variable Values" in the GNU Make manual.
$(OBJS): TOOLSET := $(TOOLSET)
$(OBJS): GYP_CFLAGS := $(DEFS_$(BUILDTYPE)) $(INCS_$(BUILDTYPE))  $(CFLAGS_$(BUILDTYPE)) $(CFLAGS_C_$(BUILDTYPE))
$(OBJS): GYP_CXXFLAGS := $(DEFS_$(BUILDTYPE)) $(INCS_$(BUILDTYPE))  $(CFLAGS_$(BUILDTYPE)) $(CFLAGS_CC_$(BUILDTYPE))

# Suffix rules, putting all outputs into $(obj).

$(obj).$(TOOLSET)/$(TARGET)/%.o: $(srcdir)/%.cc FORCE_DO_CMD
	@$(call do_cmd,cxx,1)

# Try building from generated source, too.

$(obj).$(TOOLSET)/$(TARGET)/%.o: $(obj).$(TOOLSET)/%.cc FORCE_DO_CMD
	@$(call do_cmd,cxx,1)

$(obj).$(TOOLSET)/$(TARGET)/%.o: $(obj)/%.cc FORCE_DO_CMD
	@$(call do_cmd,cxx,1)

# End of this set of suffix rules
### Rules for final target.
LDFLAGS_Debug := -pthread \
	-Wl,-z,noexecstack \
	-fPIC \
	-B$(builddir)/../../third_party/gold

LDFLAGS_Release := -pthread \
	-Wl,-z,noexecstack \
	-fPIC \
	-B$(builddir)/../../third_party/gold \
	-Wl,-O1 \
	-Wl,--as-needed \
	-Wl,--gc-sections

LIBS := 

$(obj).target/src/modules/libwebrtc_video_coding.a: GYP_LDFLAGS := $(LDFLAGS_$(BUILDTYPE))
$(obj).target/src/modules/libwebrtc_video_coding.a: LIBS := $(LIBS)
$(obj).target/src/modules/libwebrtc_video_coding.a: TOOLSET := $(TOOLSET)
$(obj).target/src/modules/libwebrtc_video_coding.a: $(OBJS) FORCE_DO_CMD
	$(call do_cmd,alink)

all_deps += $(obj).target/src/modules/libwebrtc_video_coding.a
# Add target alias
.PHONY: webrtc_video_coding
webrtc_video_coding: $(obj).target/src/modules/libwebrtc_video_coding.a

# Add target alias to "all" target.
.PHONY: all
all: webrtc_video_coding

