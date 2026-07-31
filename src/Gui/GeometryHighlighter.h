// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <array>
#include <functional>
#include <vector>

#include <QObject>
#include <fastsignals/signal.h>

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
    std::vector<GeometryReference>& slot(HighlightRole role);
    const std::vector<GeometryReference>& slot(HighlightRole role) const;

    SelectionPredicate _isSelected;
    std::array<std::vector<GeometryReference>, highlightRoleCount> _byRole;
};

/**
 * Renders a set of geometry references on top in the 3D view.
 *
 * Instantiable and independent: several highlighters can be live at once, and
 * none of them touches the application selection. Highlighting a reference
 * never rebuilds geometry — it re-renders the nodes already in the scene.
 *
 * A hidden object is not revealed; that stays the caller's decision.
 */
class GuiExport GeometryHighlighter: public QObject
{
    Q_OBJECT

public:
    explicit GeometryHighlighter(QObject* parent = nullptr);
    ~GeometryHighlighter() override;

    /// Replaces everything highlighted under @p role.
    void setHighlighted(HighlightRole role, std::vector<GeometryReference> references);
    void clear(HighlightRole role);
    void clear();

    const GeometryHighlightModel& model() const
    {
        return _model;
    }

private:
    /// Rebuilds both roles in every 3D view that currently shows a reference.
    void refresh();

    GeometryHighlightModel _model;
    fastsignals::scoped_connection _objectDeletedConnection;
    fastsignals::scoped_connection _documentDeletedConnection;
};

}  // namespace Gui
