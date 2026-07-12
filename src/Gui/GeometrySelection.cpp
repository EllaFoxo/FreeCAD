// SPDX-License-Identifier: LGPL-2.1-or-later

#include <algorithm>
#include <functional>
#include <memory>
#include <utility>

#include <QApplication>
#include <QString>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Gui/Selection/Selection.h>
#include <Gui/Selection/SelectionFilter.h>

#include "GeometrySelection.h"

using namespace Gui;

GeometrySelection::GeometrySelection(GeometryQuantity mode, QObject* parent)
    : QObject(parent)
    , Gui::SelectionObserver(false)
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
    attachSelection();
    Q_EMIT selectionModeEntered();
}

void GeometrySelection::stopSelecting()
{
    if (!_selecting) {
        return;
    }
    _selecting = false;
    detachSelection();
    if (_gateFactory) {
        Gui::Selection().rmvSelectionGate();
    }
    Q_EMIT selectionModeExited();
}

bool GeometrySelection::appendRequested() const
{
    return _quantity == GeometryQuantity::AllowMultiple
        && (QApplication::keyboardModifiers() & Qt::ControlModifier) != 0;
}

void GeometrySelection::onSelectionChanged(const Gui::SelectionChanges& msg)
{
    if (!_selecting) {
        return;
    }
    if (msg.Type != Gui::SelectionChanges::AddSelection) {
        return;
    }

    App::Document* document = App::GetApplication().getDocument(msg.pDocName);
    App::DocumentObject* object = document ? document->getObject(msg.pObjectName) : nullptr;
    if (!object) {
        return;
    }

    GeometryReference picked {
        .object = object,
        .subName = msg.pSubName ? std::string(msg.pSubName) : std::string()
    };

    std::vector<GeometryReference> next;
    if (appendRequested()) {
        next = _references;
        if (std::ranges::find(next, picked) == next.end()) {
            next.push_back(std::move(picked));
        }
    }
    else {
        next.push_back(std::move(picked));
    }
    updateReferences(std::move(next));
}
