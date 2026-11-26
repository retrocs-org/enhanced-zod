# The original disc, extraction intermediates and backups are not runtime data.
if(NOT DEFINED SOURCE OR NOT DEFINED DESTINATION OR SOURCE STREQUAL "" OR DESTINATION STREQUAL "")
  message(FATAL_ERROR "SOURCE and DESTINATION are required")
endif()
# Only redistributable UI dependencies and notices belong in an application.
# Original levels, art, sound, videos and legacy data come from the user's imports.
file(REMOVE_RECURSE "${DESTINATION}")
file(MAKE_DIRECTORY "${DESTINATION}/frontend/fonts" "${DESTINATION}/soundfonts")
foreach(FONT Rajdhani-SemiBold.ttf Rajdhani-Medium.ttf Orbitron.ttf OFL-Rajdhani.txt OFL-Orbitron.txt)
  if(EXISTS "${SOURCE}/frontend/fonts/${FONT}")
    file(COPY "${SOURCE}/frontend/fonts/${FONT}" DESTINATION "${DESTINATION}/frontend/fonts")
  endif()
endforeach()
foreach(FONT GeneralUser-GS.sf2 GeneralUser-GS-LICENSE.txt README.txt)
  if(EXISTS "${SOURCE}/soundfonts/${FONT}")
    file(COPY "${SOURCE}/soundfonts/${FONT}" DESTINATION "${DESTINATION}/soundfonts")
  endif()
endforeach()

if(EXISTS "${SOURCE}/portal-ca.crt")
  file(COPY "${SOURCE}/portal-ca.crt" DESTINATION "${DESTINATION}")
endif()
