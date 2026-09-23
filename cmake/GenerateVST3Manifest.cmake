if(NOT DEFINED HELPER OR NOT DEFINED WINE OR NOT DEFINED OUTPUT
   OR NOT DEFINED EXPECTED_NAME)
    message(FATAL_ERROR "HELPER, WINE, OUTPUT, and EXPECTED_NAME are required")
endif()

get_filename_component(output_directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")
set(temporary_output "${OUTPUT}.tmp")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env WINEDEBUG=-all "${WINE}" "${HELPER}"
    OUTPUT_FILE "${temporary_output}"
    ERROR_VARIABLE helper_error
    RESULT_VARIABLE helper_result
    TIMEOUT 30)

if(NOT helper_result EQUAL 0)
    file(REMOVE "${temporary_output}")
    message(FATAL_ERROR
        "Could not generate the VST3 module manifest with Wine "
        "(exit ${helper_result}): ${helper_error}")
endif()

file(READ "${temporary_output}" manifest)
string(FIND "${manifest}" "\"Name\": \"${EXPECTED_NAME}\"" name_position)
string(FIND "${manifest}" "\"Classes\"" classes_position)
if(name_position EQUAL -1 OR classes_position EQUAL -1)
    file(REMOVE "${temporary_output}")
    message(FATAL_ERROR "The generated VST3 module manifest is incomplete")
endif()

file(RENAME "${temporary_output}" "${OUTPUT}")
