// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <functional>
#include <vector>

#include <FCGlobal.h>
#include <Gui/GeometryReference.h>

namespace App
{
class Document;
class DocumentObject;
}  // namespace App

namespace Gui
{

/**
 * Which references each highlight role should render.
 *
 * Holds one set of references per role and answers, for any role, the subset
 * that is not already being drawn by another mechanism. Contains no Coin or
 * view code, so it can be exercised without a 3D view.
 */
class GuiExport GeometryHighlightModel
{
public:
    /// Reports whether a reference is currently part of the 3D selection.
    using SelectionPredicate = std::function<bool(const GeometryReference&)>;

    /// Defaults to the live application selection when no predicate is given.
    explicit GeometryHighlightModel(SelectionPredicate isSelected = {});

    /// Replaces everything held under @p role.
    void setHighlighted(HighlightRole role, std::vector<GeometryReference> references);
    void clear(HighlightRole role);
    void clear();

    /// The references @p role should actually render.
    std::vector<GeometryReference> effective(HighlightRole role) const;

    /// Forgets every reference to @p object.
    void dropObject(const App::DocumentObject* object);
    /// Forgets every reference to an object owned by @p document.
    void dropDocument(const App::Document* document);

private:
    SelectionPredicate _isSelected;
    std::vector<GeometryReference> _reference;
    std::vector<GeometryReference> _hovered;
};

}  // namespace Gui
