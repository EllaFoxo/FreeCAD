/****************************************************************************
 *   Copyright (c) 2022 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

#pragma once

#include <map>
#include <string>
#include <Inventor/SbColor.h>
#include <Base/Color.h>
#include <Gui/GeometryReference.h>
#include <Gui/Selection/Selection.h>

class SoDrawStyle;
class SoGroup;
class SoNode;
class SoSeparator;
class SoTempPath;

namespace App
{
class DocumentObject;
}

namespace Gui
{

class Document;
class SoFCUnifiedSelection;
class ViewProviderDocumentObject;

class GuiExport View3DInventorSelection
{
public:
    View3DInventorSelection(SoFCUnifiedSelection* root);
    ~View3DInventorSelection();

    void setDocument(Gui::Document* pcDocument)
    {
        guiDocument = pcDocument;
    }
    Gui::Document* getDocument() const
    {
        return guiDocument;
    }

    void checkGroupOnTop(const SelectionChanges& Reason);
    void clearGroupOnTop();

    /// Sets the colour and line width every subsequent highlight of @p role uses.
    void setHighlightStyle(HighlightRole role, const Base::Color& color, float lineWidth);
    /// Renders @p object's @p subName on top in @p role's style. A whole-object
    /// reference passes an empty @p subName. Silently does nothing when the
    /// reference cannot be resolved, or when its object is hidden.
    void addHighlight(HighlightRole role, App::DocumentObject* object, const char* subName);
    /// Removes every highlight of @p role.
    void clearHighlight(HighlightRole role);

private:
    /// Prefixes @p path with the scene-graph path through every enclosing
    /// geo-feature group. False when @p vp is hidden inside one of them, which
    /// means no on-top rendering is possible.
    bool appendGroupPath(ViewProviderDocumentObject* vp, SoTempPath& path) const;

    SoGroup* highlightGroup(HighlightRole role) const;

    SoGroup* pcGroupOnTop;
    SoGroup* pcGroupOnTopSel;
    SoGroup* pcGroupOnTopPreSel;
    SoGroup* pcGroupHighlight;
    SoGroup* pcHighlightReference;
    SoGroup* pcHighlightHovered;
    SoDrawStyle* pcHighlightReferenceStyle;
    SoDrawStyle* pcHighlightHoveredStyle;
    SbColor highlightReferenceColor;
    SbColor highlightHoveredColor;
    SoFCUnifiedSelection* selectionRoot;
    std::map<std::string, SoNode*> objectsOnTop;
    std::map<std::string, SoNode*> objectsOnTopPreSel;
    Gui::Document* guiDocument = nullptr;
};

}  // namespace Gui
