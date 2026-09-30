# - Find OpenColorIO
# Manually authored — precompiled lib doesn't ship a config file.

set(OpenColorIO_ROOT "${CMAKE_SOURCE_DIR}/lib/opencolorio")

find_path(OpenColorIO_INCLUDE_DIR
  NAMES OpenColorIO/OpenColorIO.h
  HINTS ${OpenColorIO_ROOT}
  PATH_SUFFIXES include
  NO_DEFAULT_PATH
)

find_library(OpenColorIO_LIBRARY
  NAMES OpenColorIO
  HINTS ${OpenColorIO_ROOT}
  PATH_SUFFIXES lib
  NO_DEFAULT_PATH
)

# Version from OpenColorABI.h
if(EXISTS "${OpenColorIO_INCLUDE_DIR}/OpenColorIO/OpenColorABI.h")
  file(STRINGS "${OpenColorIO_INCLUDE_DIR}/OpenColorIO/OpenColorABI.h"
       _ocio_ver REGEX "#define OCIO_VERSION_(MAJOR|MINOR|PATCH)")
  string(REGEX MATCH "OCIO_VERSION_MAJOR ([0-9]+)" _m "${_ocio_ver}")
  set(OpenColorIO_VERSION_MAJOR "${CMAKE_MATCH_1}")
  string(REGEX MATCH "OCIO_VERSION_MINOR ([0-9]+)" _m "${_ocio_ver}")
  set(OpenColorIO_VERSION_MINOR "${CMAKE_MATCH_1}")
  string(REGEX MATCH "OCIO_VERSION_PATCH ([0-9]+)" _m "${_ocio_ver}")
  set(OpenColorIO_VERSION_PATCH "${CMAKE_MATCH_1}")
  set(OpenColorIO_VERSION "${OpenColorIO_VERSION_MAJOR}.${OpenColorIO_VERSION_MINOR}.${OpenColorIO_VERSION_PATCH}")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OpenColorIO
  REQUIRED_VARS OpenColorIO_LIBRARY OpenColorIO_INCLUDE_DIR
  VERSION_VAR OpenColorIO_VERSION
)

if(OpenColorIO_FOUND)
  set(OpenColorIO_LIBRARIES ${OpenColorIO_LIBRARY})
  set(OpenColorIO_INCLUDE_DIRS ${OpenColorIO_INCLUDE_DIR})
  if(NOT TARGET OpenColorIO::OpenColorIO)
    add_library(OpenColorIO::OpenColorIO UNKNOWN IMPORTED)
    set_target_properties(OpenColorIO::OpenColorIO PROPERTIES
      IMPORTED_LOCATION "${OpenColorIO_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${OpenColorIO_INCLUDE_DIR}")
  endif()
endif()

mark_as_advanced(OpenColorIO_INCLUDE_DIR OpenColorIO_LIBRARY)
