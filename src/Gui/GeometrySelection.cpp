// SPDX-License-Identifier: LGPL-2.1-or-later

#include <functional>
#include <memory>
#include <utility>

#include <QString>

#include <Gui/Selection/Selection.h>
#include <Gui/Selection/SelectionFilter.h>

#include "GeometrySelection.h"

using namespace Gui;

GeometrySelection::GeometrySelection(GeometryQuantity mode, QObject* parent)
    : QObject(parent)
    , _quantity(mode)
{}

GeometrySelection::~GeometrySelection()
{
    // Guaranteed-pairing contract: never leave a session dangling.
    if (_selecting) {
        stopSelecting();
    }
}

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

void GeometrySelection::setSelectionGate(GateFactory factory)
{
    _gateFactory = std::move(factory);
}

void GeometrySelection::setSelectionFilter(const QString& filter)
{
    const std::string filterString = filter.toStdString();
    _gateFactory = [filterString] {
        return std::make_unique<SelectionFilterGate>(filterString.c_str());
    };
}

void GeometrySelection::startSelecting()
{
    if (_selecting) {
        return;
    }
    if (_gateFactory) {
        // Selection takes ownership and deletes on rmvSelectionGate.
        Gui::Selection().addSelectionGate(_gateFactory().release());
    }
    _selecting = true;
    Q_EMIT selectionModeEntered();
}

void GeometrySelection::stopSelecting()
{
    if (!_selecting) {
        return;
    }
    _selecting = false;
    if (_gateFactory) {
        Gui::Selection().rmvSelectionGate();
    }
    Q_EMIT selectionModeExited();
}
