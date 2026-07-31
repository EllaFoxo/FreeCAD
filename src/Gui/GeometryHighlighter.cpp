// SPDX-License-Identifier: LGPL-2.1-or-later

#include <algorithm>
#include <array>
#include <map>
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

std::vector<GeometryReference>& GeometryHighlightModel::slot(HighlightRole role)
{
    return _byRole.at(highlightRoleIndex(role));
}

const std::vector<GeometryReference>& GeometryHighlightModel::slot(HighlightRole role) const
{
    return _byRole.at(highlightRoleIndex(role));
}

void GeometryHighlightModel::setHighlighted(HighlightRole role, std::vector<GeometryReference> references)
{
    slot(role) = std::move(references);
}

void GeometryHighlightModel::clear(HighlightRole role)
{
    slot(role).clear();
}

void GeometryHighlightModel::clear()
{
    for (std::vector<GeometryReference>& references : _byRole) {
        references.clear();
    }
}

std::vector<GeometryReference> GeometryHighlightModel::effective(HighlightRole role) const
{
    // A hovered reference is drawn only in the hovered style, and is exempt from the
    // selection rule: hovering must give feedback even for an already-selected reference.
    if (role == HighlightRole::Hovered) {
        return slot(role);
    }

    const std::vector<GeometryReference>& hoveredReferences = slot(HighlightRole::Hovered);
    const std::vector<GeometryReference>& references = slot(role);

    std::vector<GeometryReference> result;
    result.reserve(references.size());
    for (const GeometryReference& reference : references) {
        const bool hovered = std::ranges::find(hoveredReferences, reference)
            != hoveredReferences.end();
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
    for (std::vector<GeometryReference>& references : _byRole) {
        std::erase_if(references, matches);
    }
}

void GeometryHighlightModel::dropDocument(const App::Document* document)
{
    const auto matches = [document](const GeometryReference& reference) {
        return reference.object && reference.object->getDocument() == document;
    };
    for (std::vector<GeometryReference>& references : _byRole) {
        std::erase_if(references, matches);
    }
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

/// One document's share of what each role has to render.
using ReferencesByRole = std::array<std::vector<GeometryReference>, highlightRoleCount>;

/// Splits everything @p model wants rendered by the document it lives in. An
/// annotation holds a path into its own document's scene graph, so it may only ever
/// be pushed into a viewer of that document.
std::map<App::Document*, ReferencesByRole> groupByDocument(const GeometryHighlightModel& model)
{
    std::map<App::Document*, ReferencesByRole> byDocument;
    for (std::size_t index = 0; index < highlightRoleCount; ++index) {
        for (const GeometryReference& reference : model.effective(static_cast<HighlightRole>(index))) {
            App::Document* document = reference.object ? reference.object->getDocument() : nullptr;
            if (!document) {
                continue;
            }
            byDocument[document].at(index).push_back(reference);
        }
    }
    return byDocument;
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

void GeometryHighlighter::withdrawAndAdopt(std::set<App::Document*> documents)
{
    // The documents drawn in last time as well: a role emptied since then must stop
    // rendering in the views that used to show it. Those are compared by pointer
    // against the still-open documents rather than dereferenced, so one closed
    // meanwhile is simply dropped.
    const std::vector<App::Document*> open = App::GetApplication().getDocuments();
    std::set<App::Document*> stale;
    for (App::Document* document : _touchedDocuments) {
        if (std::ranges::find(open, document) != open.end()) {
            stale.insert(document);
        }
    }
    stale.insert(documents.begin(), documents.end());
    _touchedDocuments = std::move(documents);

    for (App::Document* document : stale) {
        for (View3DInventorViewer* viewer : viewersOf(document)) {
            View3DInventorSelection* selection = viewer->getInventorSelection();
            if (!selection) {
                continue;
            }
            for (std::size_t index = 0; index < highlightRoleCount; ++index) {
                selection->clearHighlight(static_cast<HighlightRole>(index), this);
            }
        }
    }
}

void GeometryHighlighter::refresh()
{
    const std::map<App::Document*, ReferencesByRole> byDocument = groupByDocument(_model);

    std::set<App::Document*> documents;
    for (const auto& [document, references] : byDocument) {
        documents.insert(document);
    }
    // Withdraw before anything below can bail out, so a clear() never strands an
    // annotation in a view.
    withdrawAndAdopt(std::move(documents));

    if (byDocument.empty()) {
        return;
    }

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
    const std::array<RoleStyle, highlightRoleCount> styles {
        RoleStyle {
            .role = HighlightRole::Reference,
            .color = parameters->resolve(StyleParameters::GeometryHighlightReferenceColor),
            .lineWidth = static_cast<float>(
                parameters->resolve(StyleParameters::GeometryHighlightReferenceLineWidth).value
            )
        },
        RoleStyle {
            .role = HighlightRole::Hovered,
            .color = parameters->resolve(StyleParameters::GeometryHighlightHoveredColor),
            .lineWidth = static_cast<float>(
                parameters->resolve(StyleParameters::GeometryHighlightHoveredLineWidth).value
            )
        },
    };

    for (const auto& [document, references] : byDocument) {
        for (View3DInventorViewer* viewer : viewersOf(document)) {
            View3DInventorSelection* selection = viewer->getInventorSelection();
            if (!selection) {
                continue;
            }
            for (const RoleStyle& style : styles) {
                selection->setHighlightStyle(style.role, style.color, style.lineWidth);
                for (const GeometryReference& reference :
                     references.at(highlightRoleIndex(style.role))) {
                    selection
                        ->addHighlight(style.role, this, reference.object, reference.subName.c_str());
                }
            }
        }
    }
}
