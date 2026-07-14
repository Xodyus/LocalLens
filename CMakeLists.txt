cmake_minimum_required(VERSION 3.21)
project(LocalLens VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Sql)
qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(locallens
    src/main.cpp
)

# QML files live under src/ui/qml/ on disk but must sit at the module root
# in the resource system, so alias each one to its bare filename.
set(LOCALLENS_QML_FILES
    src/ui/qml/Main.qml
)
foreach(qml_file IN LISTS LOCALLENS_QML_FILES)
    get_filename_component(qml_alias ${qml_file} NAME)
    set_source_files_properties(${qml_file} PROPERTIES QT_RESOURCE_ALIAS ${qml_alias})
endforeach()

qt_add_qml_module(locallens
    URI LocalLens
    VERSION 1.0
    QML_FILES ${LOCALLENS_QML_FILES}
)

target_include_directories(locallens PRIVATE src)
target_link_libraries(locallens PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Sql
)

enable_testing()
add_subdirectory(tests)
