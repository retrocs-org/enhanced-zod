# Shared by direct binary builds and the app's always-run resource target.
if(NOT DEFINED SOURCE OR NOT DEFINED DESTINATION OR SOURCE STREQUAL "" OR DESTINATION STREQUAL "")
  message(FATAL_ERROR "SOURCE and DESTINATION are required")
endif()
set(PROJECT_SOURCE "${SOURCE}")
set(RESOURCES "${DESTINATION}")
set(SOURCE "${PROJECT_SOURCE}/assets")
set(DESTINATION "${RESOURCES}/assets")
include("${CMAKE_CURRENT_LIST_DIR}/../copy-runtime-assets.cmake")
file(REMOVE_RECURSE "${RESOURCES}/maps")
file(MAKE_DIRECTORY "${RESOURCES}/maps")
file(COPY "${PROJECT_SOURCE}/map_list.txt" "${PROJECT_SOURCE}/default_settings.txt"
     DESTINATION "${RESOURCES}")
configure_file("${PROJECT_SOURCE}/LICENSE" "${RESOURCES}/LICENSE.txt" COPYONLY)
