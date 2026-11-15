# =============================================================================
# tools/CMakeLists.txt
# =============================================================================
# Development-only measurement tools. These targets are intentionally not
# installed and are not part of the product CLI surface.

add_library(fqc_ratio_probe_lib STATIC
    ratio_probe/ratio_probe.cpp
)

target_include_directories(fqc_ratio_probe_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/ratio_probe
)

target_compile_features(fqc_ratio_probe_lib PUBLIC cxx_std_23)
target_link_libraries(fqc_ratio_probe_lib PUBLIC fqc_core)

add_executable(fqc_ratio_probe
    ratio_probe/main.cpp
)

target_link_libraries(fqc_ratio_probe
    PRIVATE
        fqc_ratio_probe_lib
        CLI11::CLI11
)

add_library(fqc_ref_archive_lib STATIC
    ref_archive/ref_archive.cpp
)

target_include_directories(fqc_ref_archive_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/ref_archive
)

target_compile_features(fqc_ref_archive_lib PUBLIC cxx_std_23)
target_link_libraries(fqc_ref_archive_lib PUBLIC fqc_core)

add_executable(fqc_ref_archive
    ref_archive/main.cpp
)

target_link_libraries(fqc_ref_archive
    PRIVATE
        fqc_ref_archive_lib
        CLI11::CLI11
)
