// SPDX-License-Identifier: LGPL-2.1-or-later

#include <algorithm>
#include <set>
#include <utility>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Base/ServiceProvider.h>
#include <Gui/Application.h>
#include <Gui/Document.h>
#include <Gui/Selection/Selection.h>
#include <Gui/StyleParameters.h>
#include <Gui/StyleParameters/ParameterManager.h>
#include <Gui/View3DInventor.h>
#include <Gui/View3DInventorSelection.h>
#include <Gui/View3DInventorViewer.h>
#include <Gui/ViewProviderDocumentObject.h>

#include "GeometryHighlighter.h"

using namespace Gui;

namespace
{
/// Membership in the live 3D selection. The default ResolveMode resolves the
/// group anchoring that GeometrySelection::seedViewportSelection() builds, so
/// no path has to be reconstructed here.
bool isInViewportSelection(const GeometryReference& reference)
{
    if (!reference.object) {
        return false;
    }
    return Gui::Selection().isSelected(reference.object, reference.subName.c_str());
}
}  // namespace

GeometryHighlightModel::GeometryHighlightModel(SelectionPredicate isSelected)
    : _isSelected(isSelected ? std::move(isSelected) : SelectionPredicate(isInViewportSelection))
{}

void GeometryHighlightModel::setHighlighted(HighlightRole role, std::vector<GeometryReference> references)
{
    (role == HighlightRole::Hovered ? _hovered : _reference) = std::move(references);
}

void GeometryHighlightModel::clear(HighlightRole role)
{
    (role == HighlightRole::Hovered ? _hovered : _reference).clear();
}

void GeometryHighlightModel::clear()
{
    _reference.clear();
    _hovered.clear();
}

std::vector<GeometryReference> GeometryHighlightModel::effective(HighlightRole role) const
{
    // A hovered reference is drawn only in the hovered style, and is exempt from the
    // selection rule: hovering must give feedback even for an already-selected reference.
    if (role == HighlightRole::Hovered) {
        return _hovered;
    }

    std::vector<GeometryReference> result;
    result.reserve(_reference.size());
    for (const GeometryReference& reference : _reference) {
        const bool hovered = std::ranges::find(_hovered, reference) != _hovered.end();
        if (hovered || _isSelected(reference)) {
            continue;
        }
        result.push_back(reference);
    }
    return result;
}

void GeometryHighlightModel::dropObject(const App::DocumentObject* object)
{
    const auto matches = [object](const GeometryReference& reference) {
        return reference.object == object;
    };
    std::erase_if(_reference, matches);
    std::erase_if(_hovered, matches);
}

void GeometryHighlightModel::dropDocument(const App::Document* document)
{
    const auto matches = [document](const GeometryReference& reference) {
        return reference.object && reference.object->getDocument() == document;
    };
    std::erase_if(_reference, matches);
    std::erase_if(_hovered, matches);
}

namespace
{
/// Every 3D viewer showing @p document, resolved fresh: a viewer can be destroyed
/// at any time and takes its highlight group with it, so none are cached.
std::vector<View3DInventorViewer*> viewersOf(App::Document* document)
{
    std::vector<View3DInventorViewer*> viewers;
    if (!document || !Application::Instance) {
        return viewers;
    }
    Gui::Document* guiDocument = Application::Instance->getDocument(document);
    if (!guiDocument) {
        return viewers;
    }
    for (MDIView* view : guiDocument->getMDIViewsOfType(View3DInventor::getClassTypeId())) {
        if (auto* inventorView = freecad_cast<View3DInventor*>(view)) {
            viewers.push_back(inventorView->getViewer());
        }
    }
    return viewers;
}
}  // namespace

GeometryHighlighter::GeometryHighlighter(QObject* parent)
    : QObject(parent)
{
    if (!Application::Instance) {
        return;
    }
    _objectDeletedConnection = Application::Instance->signalDeletedObject.connect(
        [this](const ViewProvider& viewProvider) {
            const auto* documentObject = freecad_cast<const ViewProviderDocumentObject*>(&viewProvider);
            if (!documentObject) {
                return;
            }
            _model.dropObject(documentObject->getObject());
            refresh();
        }
    );
    _documentDeletedConnection = Application::Instance->signalDeleteDocument.connect(
        [this](const Gui::Document& document) {
            _model.dropDocument(document.getDocument());
            refresh();
        }
    );
}

GeometryHighlighter::~GeometryHighlighter()
{
    clear();
}

void GeometryHighlighter::setHighlighted(HighlightRole role, std::vector<GeometryReference> references)
{
    _model.setHighlighted(role, std::move(references));
    refresh();
}

void GeometryHighlighter::clear(HighlightRole role)
{
    _model.clear(role);
    refresh();
}

void GeometryHighlighter::clear()
{
    _model.clear();
    refresh();
}

void GeometryHighlighter::refresh()
{
    auto* parameters = Base::provideService<Gui::StyleParameters::ParameterManager>();
    if (!parameters) {
        return;
    }

    // Resolved per rebuild rather than cached: highlights are transient, so a theme
    // change is picked up by the next one.
    struct RoleStyle
    {
        HighlightRole role;
        Base::Color color;
        float lineWidth;
    };
    // resolve(ParameterDefinition<T>) returns T, so the colour arrives as a
    // Base::Color and the width as a Numeric — no Value unwrapping here.
    const RoleStyle styles[] = {
        {.role = HighlightRole::Reference,
         .color = parameters->resolve(StyleParameters::GeometryHighlightReferenceColor),
         .lineWidth = static_cast<float>(
             parameters->resolve(StyleParameters::GeometryHighlightReferenceLineWidth).value
         )},
        {.role = HighlightRole::Hovered,
         .color = parameters->resolve(StyleParameters::GeometryHighlightHoveredColor),
         .lineWidth = static_cast<float>(
             parameters->resolve(StyleParameters::GeometryHighlightHoveredLineWidth).value
         )},
    };

    // Every viewer of every open document, not just the ones currently holding a
    // highlight: a role emptied since the last refresh must still be cleared in the
    // views that used to show it.
    std::set<View3DInventorViewer*> viewers;
    for (App::Document* document : App::GetApplication().getDocuments()) {
        for (View3DInventorViewer* viewer : viewersOf(document)) {
            viewers.insert(viewer);
        }
    }

    for (View3DInventorViewer* viewer : viewers) {
        View3DInventorSelection* selection = viewer->getInventorSelection();
        if (!selection) {
            continue;
        }
        for (const RoleStyle& style : styles) {
            selection->clearHighlight(style.role);
            selection->setHighlightStyle(style.role, style.color, style.lineWidth);
            for (const GeometryReference& reference : _model.effective(style.role)) {
                selection->addHighlight(style.role, reference.object, reference.subName.c_str());
            }
        }
    }
}
