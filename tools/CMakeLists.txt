# Kyty-012: Standalone Reverse Engineering Tools
#
# These tools are independent of the emulator binary. Each tool reads a
# PS5 file format and prints human-readable info — useful for debugging
# without launching the full emulator.
#
# Tools:
#   kytyps5-elf-info      — read PS5 ELF/SELF header info
#   kytyps5-nids          — query NID database (data/nids.csv)
#   kytyps5-param-sfo     — read PS5 param.json
#   kytyps5-pkg-inspect    — view PKG file contents without extraction
#
# Build: emitted by CMake's main build, alongside kyty_emulator + launcher.
# Install: installed to the same directory as the emulator.

set(TOOLS_INSTALL_DIR "." CACHE PATH "Where to install RE tools")

# elf_info — pure C++ stdlib, no dependencies.
add_executable(kytyps5_elf_info elf_info.cpp)
set_target_properties(kytyps5_elf_info PROPERTIES OUTPUT_NAME kytyps5-elf-info)
target_compile_features(kytyps5_elf_info PRIVATE cxx_std_17)

# nids_tool — pure C++ stdlib, reads data/nids.csv at runtime.
add_executable(kytyps5_nids nids_tool.cpp)
set_target_properties(kytyps5_nids PROPERTIES OUTPUT_NAME kytyps5-nids)
target_compile_features(kytyps5_nids PRIVATE cxx_std_17)

# param_sfo_info — links nlohmann_json.
# NOTE: nlohmann_json's `json::parse` throws on malformed input, and the
# tool uses try/catch to print a friendly error. KytyPS5 globally builds
# with -fno-exceptions (set in src/utils.cmake), so we have to explicitly
# re-enable exceptions for this single file.
add_executable(kytyps5_param_sfo param_sfo_info.cpp)
set_target_properties(kytyps5_param_sfo PROPERTIES OUTPUT_NAME kytyps5-param-sfo)
target_compile_features(kytyps5_param_sfo PRIVATE cxx_std_17)
target_link_libraries(kytyps5_param_sfo PRIVATE nlohmann_json::nlohmann_json)
# Re-enable exceptions (overrides the global -fno-exceptions from utils.cmake).
if(NOT MSVC)
        target_compile_options(kytyps5_param_sfo PRIVATE -fexceptions)
endif()

# pkg_inspect — pure C++ stdlib (defines its own packed PkgHeader struct
# so it stays standalone; doesn't depend on src/loader/pkg.cpp).
add_executable(kytyps5_pkg_inspect pkg_inspect.cpp)
set_target_properties(kytyps5_pkg_inspect PROPERTIES OUTPUT_NAME kytyps5-pkg-inspect)
target_compile_features(kytyps5_pkg_inspect PRIVATE cxx_std_17)

# Install all tools next to the emulator binary so they can find data/
# (nids.csv) at runtime when launched from the install prefix.
install(TARGETS
        kytyps5_elf_info
        kytyps5_nids
        kytyps5_param_sfo
        kytyps5_pkg_inspect
        RUNTIME DESTINATION ${TOOLS_INSTALL_DIR})

# Copy tools next to the emulator/launcher at build time too, so the build
# tree is usable without `cmake --install`.
foreach(tool kytyps5_elf_info kytyps5_nids kytyps5_param_sfo kytyps5_pkg_inspect)
        add_custom_command(TARGET ${tool} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        $<TARGET_FILE:${tool}>
                        $<TARGET_FILE_DIR:launcher>/$<TARGET_FILE_NAME:${tool}>
                COMMENT "Copying ${tool} next to launcher"
                VERBATIM)
endforeach()
