// SPDX-License-Identifier: LGPL-2.1-or-later

#include <algorithm>
#include <utility>

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Gui/Selection/Selection.h>

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
