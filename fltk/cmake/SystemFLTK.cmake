# Prefer the installed system FLTK; an explicit FLTK_DIR selects another build.
if(FLTK_DIR)
  find_package(FLTK 1.4 CONFIG REQUIRED)
else()
  # Fedora's shared-devel package currently exports missing static archives in
  # its config file. CMake's standard finder uses the installed shared libraries.
  set(FLTK_SKIP_FORMS TRUE)
  set(FLTK_SKIP_FLUID TRUE)
  find_package(FLTK REQUIRED MODULE)
  foreach(component fltk gl images)
    if(component STREQUAL fltk)
      set(libname fltk)
    else()
      set(libname fltk_${component})
    endif()
    find_library(SYSTEM_${component}_LIBRARY NAMES ${libname} REQUIRED)
    add_library(fltk::${component}-shared SHARED IMPORTED)
    set_target_properties(fltk::${component}-shared PROPERTIES
      IMPORTED_LOCATION "${SYSTEM_${component}_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${FLTK_INCLUDE_DIR}")
  endforeach()
endif()
