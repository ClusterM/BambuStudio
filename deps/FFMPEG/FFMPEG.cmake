set(_conf_cmd ./configure)

if (MSVC)
    # bambulab's ffmpeg_prebuilts are x64-only; linking them on ARM64 leaves
    # every av*/sws* symbol unresolved. For Windows ARM64 use BtbN's winarm64
    # shared 7.1 build. FFmpeg 7.0/7.1 share the same library majors
    # (avcodec-61, avutil-59, swscale-8, swresample-5). Newer BtbN autobuilds
    # dropped the 7.1 assets; autobuild-2026-07-17-13-22 404s, and the 7.1
    # zip that is still published lives under autobuild-2026-07-31-14-10.
    # ARM64EC keeps the x64 package: it links x64 object code.
    set(_ffmpeg_msvc_url "https://github.com/bambulab/ffmpeg_prebuilts/releases/download/7.0.2/7.0.2_msvc.zip")
    set(_ffmpeg_msvc_hash "SHA256=DF44AE6B97CE84C720695AE7F151B4A9654915D1841C68F10D62A1189E0E7181")
    # Generator platform takes precedence over the host processor so an ARM64
    # host can still build x64 deps (and ARM64EC targets get the x64 package).
    set(_ffmpeg_gen_platform "${CMAKE_GENERATOR_PLATFORM}")
    if (NOT _ffmpeg_gen_platform)
        set(_ffmpeg_gen_platform "${CMAKE_SYSTEM_PROCESSOR}")
    endif ()
    string(TOUPPER "${_ffmpeg_gen_platform}" _ffmpeg_gen_platform)
    if (_ffmpeg_gen_platform MATCHES "^(ARM64|AARCH64)$")
        set(_ffmpeg_msvc_url "https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-07-31-14-10/ffmpeg-n7.1.5-12-g1fdbca85aa-winarm64-gpl-shared-7.1.zip")
        set(_ffmpeg_msvc_hash "SHA256=717ed0a06dcdf1b4aee835615d8dcd968b268f89035bf1aba75d055661d29b9f")
    endif ()
    set(_dstdir ${DESTDIR}/usr/local)
    set(_source_dir "${CMAKE_BINARY_DIR}/dep_FFMPEG-prefix/src/dep_FFMPEG")
    ExternalProject_Add(dep_FFMPEG
        URL ${_ffmpeg_msvc_url}
        URL_HASH ${_ffmpeg_msvc_hash}
        DOWNLOAD_DIR ${DEP_DOWNLOAD_DIR}/FFMPEG
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ""
        INSTALL_COMMAND ${CMAKE_COMMAND}
            -DSRC=${_source_dir}
            -DDST=${_dstdir}
            -P ${CMAKE_CURRENT_LIST_DIR}/install_ffmpeg.cmake
    )

else ()
    set(_extra_cmd "--pkg-config-flags=\"--static\"")
    string(APPEND _extra_cmd "--extra-cflags=\"-I ${DESTDIR}/usr/local/include\"")
    string(APPEND _extra_cmd "--extra-ldflags=\"-I ${DESTDIR}/usr/local/lib\"")
    string(APPEND _extra_cmd "--extra-libs=\"-lpthread -lm\"")
    string(APPEND _extra_cmd "--ld=\"g++\"")
    string(APPEND _extra_cmd "--bindir=\"${DESTDIR}/usr/local/bin\"")
    string(APPEND _extra_cmd "--enable-gpl")
    string(APPEND _extra_cmd "--enable-nonfree")

    if (APPLE)
        set(_minos_cmd 
            "CFLAGS=-mmacosx-version-min=${DEP_OSX_TARGET}"
            "LDFLAGS=-mmacosx-version-min=${DEP_OSX_TARGET}"
            )
        if (IS_CROSS_COMPILE)
            set(_cross_cmd --enable-cross-compile)
            set(_pic_cmd --enable-pic)
            if (${CMAKE_SYSTEM_PROCESSOR} MATCHES "x86_64")
                set(_arch_cmd --arch=arm64)
                set(_cc_cmd "--cc=clang -arch arm64")
            else()
                set(_arch_cmd --arch=x86_64)
                set(_cc_cmd "--cc=clang -arch x86_64")
            endif()
        endif()
    endif()

    set(_build_j -j)
    if(DEFINED ENV{CMAKE_BUILD_PARALLEL_LEVEL})
        set(_build_j "-j$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")
    endif()

    ExternalProject_Add(dep_FFMPEG
        URL https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n7.0.2.tar.gz
        URL_HASH SHA256=5EB46D18D664A0CCADF7B0ADEE03BD3B7FA72893D667F36C69E202A807E6D533
        DOWNLOAD_DIR ${DEP_DOWNLOAD_DIR}/FFMPEG
        CONFIGURE_COMMAND ${_conf_cmd}
            ${_cross_cmd}
            ${_pic_cmd}
            ${_arch_cmd}
            ${_cc_cmd}
            --prefix="${DESTDIR}/usr/local"
            --enable-shared
            --disable-doc
            --enable-small
            --disable-outdevs
            --disable-filters
            --enable-filter=*null*,afade,*fifo,*format,*resample,aeval,allrgb,allyuv,atempo,pan,*bars,color,*key,crop,draw*,eq*,framerate,*_qsv,*_vaapi,*v4l2*,hw*,scale,volume,test*
            --disable-protocols
            --enable-protocol=file,fd,pipe,rtp,udp
            --disable-muxers
            --enable-muxer=rtp
            --disable-encoders
            --disable-decoders
            --enable-decoder=*aac*,h264*,mp3*,mjpeg,rv*
            --disable-demuxers
            --enable-demuxer=h264,mp3,mov
            --disable-zlib
            --disable-avdevice
        BUILD_IN_SOURCE ON
        BUILD_COMMAND make ${_build_j}
        INSTALL_COMMAND make install
    )

endif()