# Copy a prebuilt FFmpeg tree into the dependency prefix.
# Bambu's x64 zip is flat (bin/ at the archive root). BtbN's winarm64 zip
# wraps the same layout in a single top-level directory.
if(NOT DEFINED SRC OR NOT DEFINED DST)
  message(FATAL_ERROR "install_ffmpeg.cmake requires SRC and DST")
endif()

set(_root "${SRC}")
if(NOT EXISTS "${_root}/bin")
  file(GLOB _children LIST_DIRECTORIES true "${SRC}/*")
  foreach(_child ${_children})
    if(IS_DIRECTORY "${_child}" AND EXISTS "${_child}/bin")
      set(_root "${_child}")
      break()
    endif()
  endforeach()
endif()

if(NOT EXISTS "${_root}/bin" OR NOT EXISTS "${_root}/lib" OR NOT EXISTS "${_root}/include")
  message(FATAL_ERROR "FFmpeg bin/lib/include not found under ${SRC}")
endif()

foreach(_dir bin lib include)
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${_root}/${_dir}" "${DST}/${_dir}"
    RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "Failed to copy FFmpeg ${_dir} from ${_root}")
  endif()
endforeach()
