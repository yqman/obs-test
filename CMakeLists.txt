cmake_minimum_required(VERSION 3.5)

#add_subdirectory(obs-frontend-api)



project(obs_qt_test LANGUAGES CXX)

include_directories(AFTER "/usr/local/include/obs")

#set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/)

#find_package(libobs)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

SET(libobs_DIR /usr/local/lib)
SET(libobs_INCLUDE_DIRS /usr/local/include)
SET(libobs_LIBRARIES /usr/local/lib)

SET(libobs-frontend-api_DIR /usr/local/lib)
SET(libobs-frontend-api_INCLUDE_DIRS /usr/local/include/obs)
SET(libobs-frontend-api_LIBRARIES /usr/local/lib)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#SET(QT_USE_QTSVG TRUE)




# QtCreator supports the following variables for Android, which are identical to qmake Android variables.
# Check http://doc.qt.io/qt-5/deployment-android.html for more information.
# They need to be set before the find_package(Qt5 ...) call.

#if(ANDROID)
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
#    if (ANDROID_ABI STREQUAL "armeabi-v7a")
#        set(ANDROID_EXTRA_LIBS
#            ${CMAKE_CURRENT_SOURCE_DIR}/path/to/libcrypto.so
#            ${CMAKE_CURRENT_SOURCE_DIR}/path/to/libssl.so)
#    endif()
#endif()

find_package(Qt5 COMPONENTS Widgets REQUIRED)
#find_package(Qt5 COMPONENTS X11extras REQUIRED)
find_package(Qt5X11Extras REQUIRED)
find_package(Qt5Svg REQUIRED)



set(obs_UI
    forms/OBSBasic.ui
    forms/OBSBasicSettings.ui
    forms/OBSBasicInteraction.ui)



qt5_wrap_ui(obs_UI_HEADERS ${obs_UI})

#if(ANDROID)
#  add_library(obs_qt_test SHARED
#    main.cpp
#    mainwindow.cpp
#    mainwindow.h
#    mainwindow.ui
#    window-basic-main.cpp
#  )
#else()
  add_executable(obs_qt_test
          main.h
          main.cpp
          mainwindow.h
          window-basic-main.hpp
          window-basic-main.cpp
          window-dock.hpp
          window-dock.cpp
          window-basic-preview.hpp
          window-basic-preview.cpp
          qt-display.hpp
          qt-display.cpp
          qt-wrappers.hpp
          qt-wrappers.cpp
          display-helpers.hpp
          platform.hpp
          platform-x11.cpp
          ui_OBSBasic.h
          window-basic-status-bar.hpp
          window-basic-status-bar.cpp
          window-basic-main-outputs.hpp
          window-basic-main-outputs.cpp
          audio-encoders.hpp
          audio-encoders.cpp
          auth-base.hpp
          auth-base.cpp
          ${obs_UI_HEADERS}
         # record-button.hpp
         # record-button.cpp
          vertical-scroll-area.hpp
          vertical-scroll-area.cpp
         # source-tree.hpp
          #source-tree.cpp
          scene-tree.hpp
          scene-tree.cpp
         window-projector.hpp
         window-projector.cpp
         #api-interface.cpp
         horizontal-scroll-area.cpp
         horizontal-scroll-area.hpp
         qt-wrappers.hpp
         qt-wrappers.cpp
         ui-config.h.in
         window-basic-interaction.hpp
         window-basic-interaction.cpp
         window-basic-properties.hpp
         window-basic-properties.cpp
         properties-view.hpp
         properties-view.cpp
         double-slider.hpp
         double-slider.cpp
         slider-ignorewheel.hpp
         slider-ignorewheel.cpp
         properties-view.moc.hpp
         combobox-ignorewheel.hpp
         combobox-ignorewheel.cpp
         spinbox-ignorewheel.hpp
         window-basic-settings.hpp
         window-basic-settings.cpp
         spinbox-ignorewheel.cpp
         window-basic-stats.hpp
         window-basic-stats.cpp
         url-push-button.hpp
         url-push-button.cpp
         hotkey-edit.hpp
         hotkey-edit.cpp
         record-button.hpp
         record-button.cpp
         source-tree.hpp
         source-tree.cpp
         source-label.hpp
         source-label.cpp
         expand-checkbox.hpp
         locked-checkbox.hpp
         visibility-checkbox.hpp
        # window-basic-main-transitions.cpp
         menu-button.hpp
         menu-button.cpp
         window-namedialog.hpp
         window-namedialog.cpp
         window-basic-main-browser.cpp
         window-basic-main-icons.cpp
         window-basic-settings-stream.cpp
  )
#endif()

target_link_libraries(obs_qt_test PRIVATE Qt5::Widgets Qt5::Svg ${libobs_LIBRARIES}/libobs.so ${libobs-frontend-api_LIBRARIES}/libobs-frontend-api.so pthread.so jansson.so ${libobs_LIBRARIES}/liblibff.so )
