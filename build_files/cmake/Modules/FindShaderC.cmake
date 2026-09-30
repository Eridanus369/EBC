# Override: precompiled lib only ships libshaderc_shared.so.
set(SHADERC_LIBRARY "${CMAKE_SOURCE_DIR}/lib/shaderc/lib/libshaderc_shared.so"
    CACHE FILEPATH "" FORCE)
set(SHADERC_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/lib/shaderc/include"
    CACHE PATH "" FORCE)
set(SHADERC_LIBRARIES ${SHADERC_LIBRARY})
set(SHADERC_INCLUDE_DIRS ${SHADERC_INCLUDE_DIR})
set(SHADERC_FOUND TRUE)
mark_as_advanced(SHADERC_LIBRARY SHADERC_INCLUDE_DIR)
