set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

get_filename_component(_project_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(DEFINED ENV{XWIN_ROOT})
    file(TO_CMAKE_PATH "$ENV{XWIN_ROOT}" XWIN_ROOT)
else()
    set(XWIN_ROOT "${_project_root}/.xwin")
endif()

if(NOT EXISTS "${XWIN_ROOT}/crt/include/vector")
    message(FATAL_ERROR
        "Microsoft CRT/SDK not found. Run: "
        "xwin --accept-license --arch x86_64 splat --output ${XWIN_ROOT}")
endif()

# lld-link runs on a case-sensitive filesystem, while MSVC library names are
# case-insensitive on Windows. JUCE requests these canonical spellings.
set(_sdk_um_lib "${XWIN_ROOT}/sdk/lib/um/x86_64")
foreach(_library_pair IN ITEMS
        "DbgHelp.Lib|DbgHelp.lib"
        "dwrite.lib|Dwrite.lib"
        "d2d1.lib|D2d1.lib"
        "dcomp.lib|DComp.lib")
    string(REPLACE "|" ";" _pair "${_library_pair}")
    list(GET _pair 0 _source)
    list(GET _pair 1 _destination)
    if(NOT EXISTS "${_sdk_um_lib}/${_destination}")
        file(CREATE_LINK "${_sdk_um_lib}/${_source}"
            "${_sdk_um_lib}/${_destination}" SYMBOLIC COPY_ON_ERROR)
    endif()
endforeach()

set(_llvm_bin "/usr/lib/llvm-20/bin")
set(CMAKE_C_COMPILER "${_llvm_bin}/clang-cl")
set(CMAKE_CXX_COMPILER "${_llvm_bin}/clang-cl")
set(CMAKE_LINKER "${_llvm_bin}/lld-link")
set(CMAKE_RC_COMPILER "${_llvm_bin}/llvm-rc")
set(CMAKE_MT "${_llvm_bin}/llvm-mt")

set(CMAKE_C_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(_msvc_includes
    "${_project_root}/cmake/windows-compat"
    "${XWIN_ROOT}/crt/include"
    "${XWIN_ROOT}/sdk/include/ucrt"
    "${XWIN_ROOT}/sdk/include/shared"
    "${XWIN_ROOT}/sdk/include/um"
    "${XWIN_ROOT}/sdk/include/winrt"
    "${XWIN_ROOT}/sdk/include/cppwinrt")

set(_include_flags "")
foreach(_include IN LISTS _msvc_includes)
    string(APPEND _include_flags " /imsvc\"${_include}\"")
endforeach()

set(CMAKE_C_FLAGS_INIT "${_include_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_include_flags} /EHsc")
set(CMAKE_RC_STANDARD_INCLUDE_DIRECTORIES
    "${XWIN_ROOT}/sdk/include/ucrt"
    "${XWIN_ROOT}/sdk/include/shared"
    "${XWIN_ROOT}/sdk/include/um")

set(_library_flags
    "/libpath:\"${XWIN_ROOT}/crt/lib/x86_64\""
    "/libpath:\"${XWIN_ROOT}/sdk/lib/ucrt/x86_64\""
    "/libpath:\"${XWIN_ROOT}/sdk/lib/um/x86_64\"")
string(JOIN " " _library_flags_joined ${_library_flags})
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_library_flags_joined}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_library_flags_joined}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_library_flags_joined}")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
