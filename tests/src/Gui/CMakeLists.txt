# SPDX-License-Identifier: LGPL-2.1-or-later

add_subdirectory(Dialogs)
add_subdirectory(propertyeditor)

# Standard C++ GTest tests
add_executable(Gui_tests_run
        Assistant.cpp
        Camera.cpp
        ColorScaleOverlayTest.cpp
        FileDialog.cpp
        StyleParameters/StyleParametersApplicationTest.cpp
        StyleParameters/ParserTest.cpp
        StyleParameters/ParameterManagerTest.cpp
        StyleParameters/YamlParameterSourceTest.cpp
        StyleParameters/ColorEffectTest.cpp
        StyleParameters/ValueTest.cpp
        StyleParameters/DescriptorRegistryTest.cpp
        StyleParameters/StyleOverridesTest.cpp
        StyleParameters/ShippedThemeTest.cpp
        StyleParameters/StyleContextTest.cpp
        TreeBranchGeometryTest.cpp
        InputHintTest.cpp
        SelectionTest.cpp
        SelectionContextTest.cpp
        GeometrySelectionTest.cpp
        GeometryHighlighterTest.cpp
        TemporaryVisibilityTest.cpp
        ThumbnailCamera.cpp
)

# Qt tests
setup_qt_test(QuantitySpinBox)
setup_qt_test(GeometrySelectorWidget)
setup_qt_test(GeometrySelectorPopup)
setup_qt_test(StyleTransparency)
setup_qt_test(ComboDropdown)
setup_qt_test(TreeBranch)
setup_qt_test(ExpressionButtonLayout)
setup_qt_test(GroupBoxFrame)
setup_qt_test(StyleOverrides)
setup_qt_test(MdiSurface)
setup_qt_test(FormControlSizing)
setup_qt_test(HighlightOverlay)
setup_qt_test(MenuGeometry)

# ShippedThemeTest reads the theme YAML out of the build's data directory, so the copy has to
# have happened before the suite runs — otherwise it silently checks a stale snapshot.
add_dependencies(Gui_tests_run Stylesheets_data)

target_link_libraries(Gui_tests_run PRIVATE
    GTest::gmock_main
    FreeCADApp
    FreeCADGui
)
