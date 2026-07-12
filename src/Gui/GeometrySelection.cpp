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
#include <App/Property.h>
#include <App/PropertyLinks.h>
#include <Gui/Selection/Selection.h>
#include <Gui/Selection/SelectionFilter.h>

#include "GeometrySelection.h"
#include "GeometrySelectionGate.h"

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
    if (_autoApply && isBound()) {
        writeToProperty();
    }
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

void GeometrySelection::setAllowedKinds(GeometryKinds kinds, App::DocumentObject* support)
{
    _gateFactory = [kinds, support] {
        return std::make_unique<GeometryKindGate>(kinds, support);
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

void GeometrySelection::bind(App::Property& prop)
{
    unbind();
    _boundProperty = &prop;

    if (auto* container = dynamic_cast<App::DocumentObject*>(prop.getContainer())) {
        _propertyChangedConnection = container->getDocument()->signalChangedObject.connect(
            [this](const App::DocumentObject&, const App::Property& changed) {
                if (&changed == _boundProperty && !_writingBack) {
                    reloadFromProperty();
                }
            }
        );
    }
    reloadFromProperty();
}

void GeometrySelection::unbind()
{
    _propertyChangedConnection.disconnect();
    _boundProperty = nullptr;
}

bool GeometrySelection::apply()
{
    return writeToProperty();
}

void GeometrySelection::reloadFromProperty()
{
    std::vector<GeometryReference> loaded;
    if (auto* link = dynamic_cast<App::PropertyLinkSub*>(_boundProperty)) {
        App::DocumentObject* object = link->getValue();
        if (object) {
            const std::vector<std::string>& subs = link->getSubValues();
            if (subs.empty()) {
                loaded.push_back({.object = object, .subName = ""});
            }
            else {
                for (const std::string& sub : subs) {
                    loaded.push_back({.object = object, .subName = sub});
                }
            }
        }
    }
    else if (auto* linkList = dynamic_cast<App::PropertyLinkSubList*>(_boundProperty)) {
        const std::vector<App::DocumentObject*>& objects = linkList->getValues();
        const std::vector<std::string>& subs = linkList->getSubValues();
        for (std::size_t index = 0; index < objects.size(); ++index) {
            loaded.push_back(
                {.object = objects[index],
                 .subName = index < subs.size() ? subs[index] : std::string()}
            );
        }
    }
    // Assign directly — not via updateReferences — so reloads never trigger a write-back.
    _references = std::move(loaded);
    Q_EMIT referencesChanged();
}

bool GeometrySelection::writeToProperty()
{
    if (!isBound()) {
        return false;
    }

    _writingBack = true;
    if (auto* link = dynamic_cast<App::PropertyLinkSub*>(_boundProperty)) {
        if (_references.empty()) {
            link->setValue(nullptr, std::vector<std::string> {});
        }
        else {
            // PropertyLinkSub: single object; collect all sub names from the first object.
            App::DocumentObject* object = _references.front().object;
            std::vector<std::string> subNames;
            subNames.reserve(_references.size());
            for (const GeometryReference& ref : _references) {
                subNames.push_back(ref.subName);
            }
            link->setValue(object, subNames);
        }
    }
    else if (auto* linkList = dynamic_cast<App::PropertyLinkSubList*>(_boundProperty)) {
        std::vector<App::DocumentObject*> objects;
        std::vector<std::string> subNames;
        objects.reserve(_references.size());
        subNames.reserve(_references.size());
        for (const GeometryReference& ref : _references) {
            objects.push_back(ref.object);
            subNames.push_back(ref.subName);
        }
        linkList->setValues(objects, subNames);
    }
    _writingBack = false;
    return true;
}
