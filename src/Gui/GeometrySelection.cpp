// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"
#ifndef _PreComp_
# include <utility>
#endif

#include "GeometrySelection.h"

using namespace Gui;

GeometrySelection::GeometrySelection(GeometryQuantity mode, QObject* parent)
    : QObject(parent)
    , _quantity(mode)
{}

GeometrySelection::~GeometrySelection() = default;

void GeometrySelection::setQuantity(GeometryQuantity mode)
{
    _quantity = mode;
}

void GeometrySelection::setReferences(std::vector<GeometryReference> references)
{
    updateReferences(std::move(references));
}

void GeometrySelection::removeReference(std::size_t index)
{
    if (index >= _references.size()) {
        return;
    }
    std::vector<GeometryReference> next = _references;
    next.erase(next.begin() + static_cast<std::ptrdiff_t>(index));
    updateReferences(std::move(next));
}

void GeometrySelection::clear()
{
    if (_references.empty()) {
        return;
    }
    updateReferences({});
}

void GeometrySelection::updateReferences(std::vector<GeometryReference> references)
{
    _references = std::move(references);
    Q_EMIT referencesChanged();
}
