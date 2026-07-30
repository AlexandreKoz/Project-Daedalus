add_executable(DaedalusCoreTests CoreTests.cpp)
target_link_libraries(DaedalusCoreTests PRIVATE daedalus_core)
target_include_directories(DaedalusCoreTests PRIVATE "${PROJECT_SOURCE_DIR}/src")
daedalus_apply_project_warnings(DaedalusCoreTests)

add_test(NAME Daedalus.Core COMMAND DaedalusCoreTests)
