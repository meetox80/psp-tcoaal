execute_process(
  COMMAND git -C "${SRC}" rev-parse --short HEAD
  OUTPUT_VARIABLE hash
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
  RESULT_VARIABLE res)
if(NOT res EQUAL 0 OR hash STREQUAL "")
  set(hash "dev")
endif()
set(content "#pragma once\n#define TCOAAL_COMMIT \"${hash}\"\n")
set(old "")
if(EXISTS "${OUT}")
  file(READ "${OUT}" old)
endif()
if(NOT old STREQUAL content)
  file(WRITE "${OUT}" "${content}")
endif()
