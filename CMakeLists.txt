cmake_minimum_required(VERSION 3.21)
project(LocalLens VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Sql)
qt_standard_project_setup(REQUIRES 6.5)

# Backend logic (indexing, database, watching) lives in a static library so
# both the app and the unit tests link against the same code.
qt_add_library(locallens_core STATIC
    src/core/Tokenizer.h src/core/Tokenizer.cpp
    src/core/TextExtractor.h src/core/TextExtractor.cpp
    src/database/IndexStore.h src/database/IndexStore.cpp
)
target_include_directories(locallens_core PUBLIC src)
target_link_libraries(locallens_core PUBLIC Qt6::Core Qt6::Sql)

qt_add_executable(locallens
    src/main.cpp
)

# Qt's generated QML type registration includes headers by bare filename
# (<AppController.h>), so the ui/ directory itself must be on the include path.
target_include_directories(locallens PRIVATE src/ui)

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
    SOURCES
        src/ui/AppController.h src/ui/AppController.cpp
)

target_link_libraries(locallens PRIVATE
    locallens_core
    Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
)

enable_testing()
add_subdirectory(tests)
