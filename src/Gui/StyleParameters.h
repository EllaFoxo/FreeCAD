// SPDX-License-Identifier: LGPL-2.1-or-later

/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2025 The FreeCAD Project Association AISBL               *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#pragma once

#include <Gui/StyleParameters/ParameterManager.h>

namespace Gui::StyleParameters
{
// rubberband selection colors
DEFINE_STYLE_PARAMETER(ExpressionButtonSize, Numeric(18, "px"));  // green for touch selection
                                                                  // (right to left)

// Reference highlighting in the 3D view. Blue so it reads as distinct from
// selection green, preselection yellow, and the PartDesign preview colours.
DEFINE_STYLE_PARAMETER(GeometryHighlightReferenceColor, Base::Color(0.20F, 0.55F, 1.00F));
DEFINE_STYLE_PARAMETER(GeometryHighlightReferenceLineWidth, Numeric(3));
DEFINE_STYLE_PARAMETER(GeometryHighlightHoveredColor, Base::Color(0.45F, 0.78F, 1.00F));
DEFINE_STYLE_PARAMETER(GeometryHighlightHoveredLineWidth, Numeric(4));
}  // namespace Gui::StyleParameters
