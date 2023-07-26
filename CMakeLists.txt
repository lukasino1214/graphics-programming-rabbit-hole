cmake_minimum_required(VERSION 3.11)
include(cmake/warnings.cmake)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

execute_process(
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/compile_commands.json
)

project(test)
find_package(daxa CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(simdjson CONFIG REQUIRED)
find_package(fastgltf CONFIG REQUIRED)

function(make_example name)
    project(${name})
    add_executable(${name} "src/${name}/main.cpp" "src/impl.cpp" "src/camera.cpp" "src/texture.cpp" "src/model.cpp")
    target_compile_features(${name} PRIVATE cxx_std_20)
    target_link_libraries(${name} PRIVATE daxa::daxa glfw imgui::imgui fastgltf::fastgltf glm::glm)
    target_include_directories(${name} PRIVATE ${Stb_INCLUDE_DIR})
    target_compile_definitions(${name} PRIVATE DAXA_SHADER_INCLUDE_DIR="$<TARGET_FILE_DIR:${name}>/../vcpkg_installed/x64-$<LOWER_CASE:$<PLATFORM_ID>>/include")
endfunction()

make_example(triangle)
make_example(textured_quad)
make_example(bloom)
make_example(basic_forward)
make_example(forward)
make_example(deferred)
make_example(ssao)
make_example(directional_shadow)
make_example(basic_compute)
make_example(compute_triangle)
make_example(normal_mapping)
make_example(parallax_mapping)
make_example(tiled_forward)
make_example(spot_shadow)
make_example(variance_shadow)
make_example(fxaa)
