// SPDX-License-Identifier: LGPL-2.1-or-later

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

#include <QApplication>
#include <QString>
#include <QTimer>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/GeoFeatureGroupExtension.h>
#include <App/Property.h>
#include <App/PropertyLinks.h>
#include <Gui/Application.h>
#include <Gui/GeometryHighlighter.h>
#include <Gui/Selection/Selection.h>
#include <Gui/Selection/SelectionFilter.h>
#include <Gui/ViewProviderDocumentObject.h>

#include "GeometrySelection.h"
#include "GeometrySelectionGate.h"

using namespace Gui;

GeometrySelection::GeometrySelection(GeometryQuantity mode, QObject* parent)
    : QObject(parent)
    , Gui::SelectionObserver(false)
    , _quantity(mode)
    , _highlighter(std::make_unique<GeometryHighlighter>())
{}

GeometrySelection::~GeometrySelection()
{
    // Guaranteed-pairing contract: never leave a session dangling. Block our Qt
    // signals before unwinding: they are wired to slots on the owning widget, which
    // may already be partway through its own destruction (Qt clears a QObject's
    // connections only in ~QObject, which runs after ~QWidget has deleted its
    // children). Emitting selectionModeExited here would otherwise invoke a slot on
    // that half-destroyed object and abort.
    if (_selecting) {
        blockSignals(true);
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

void GeometrySelection::setHoveredReference(int index)
{
    const bool valid = index >= 0 && index < static_cast<int>(_references.size());
    _hoveredIndex = valid ? index : -1;
    if (_hoveredIndex < 0) {
        _highlighter->clear(HighlightRole::Hovered);
        return;
    }
    _highlighter->setHighlighted(
        HighlightRole::Hovered,
        {_references[static_cast<std::size_t>(_hoveredIndex)]}
    );
}

void GeometrySelection::refreshHighlight()
{
    _highlighter->setHighlighted(HighlightRole::Reference, _references);
    // Re-resolve the hover against the new model; a removal can invalidate it.
    setHoveredReference(_hoveredIndex);
}

void GeometrySelection::updateReferences(std::vector<GeometryReference> references)
{
    _references = std::move(references);
    if (_autoApply && isBound()) {
        writeToProperty();
    }
    refreshHighlight();
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
    _referencesBeforeSelecting = _references;
    if (_gateFactory) {
        // Selection takes ownership and deletes on rmvSelectionGate.
        Gui::Selection().addSelectionGate(_gateFactory().release());
    }
    _selecting = true;
    // Let observers react first (e.g. hide this feature's preview and show the
    // previous feature). Only then reveal and highlight the references, so their
    // scene rebuild cannot clear the seeded selection.
    Q_EMIT selectionModeEntered();
    showReferencedObjects();
    seedViewportSelection();
    // Attach last, so the seeding picks above are not fed back into our own
    // onSelectionChanged().
    attachSelection();
    // Seeding just moved the references into the real 3D selection, so the
    // highlighter's Reference role now has less to draw.
    refreshHighlight();
}

void GeometrySelection::stopSelecting()
{
    if (!_selecting) {
        return;
    }
    _selecting = false;
    detachSelection();
    restoreReferencedObjects();
    if (_gateFactory) {
        Gui::Selection().rmvSelectionGate();
    }
    Q_EMIT selectionModeExited();
    // Teardown just took the references back out of the real 3D selection, so the
    // highlighter's Reference role must draw them again.
    refreshHighlight();
}

void GeometrySelection::showReferencedObjects()
{
    if (!Gui::Application::Instance) {
        return;
    }
    for (const GeometryReference& ref : _references) {
        auto* viewProvider
            = Gui::Application::Instance->getViewProvider<Gui::ViewProviderDocumentObject>(ref.object);
        if (viewProvider && !viewProvider->Visibility.getValue()) {
            viewProvider->Visibility.setValue(true);
            _forcedVisibility.push_back(
                {.object = App::DocumentObjectWeakPtrT(ref.object), .restoreTo = false}
            );
        }
    }
}

void GeometrySelection::restoreReferencedObjects()
{
    // Re-fetch each view provider through the weak pointer: an object deleted while
    // selecting (e.g. by an aborted command) resolves to null and is simply skipped,
    // so this never dereferences a freed view provider during teardown.
    if (Gui::Application::Instance) {
        for (const ForcedVisibility& forced : _forcedVisibility) {
            App::DocumentObject* object = forced.object.get<App::DocumentObject>();
            if (!object) {
                continue;
            }
            if (auto* viewProvider = Gui::Application::Instance
                                         ->getViewProvider<Gui::ViewProviderDocumentObject>(object)) {
                viewProvider->Visibility.setValue(forced.restoreTo);
            }
        }
    }
    _forcedVisibility.clear();
}

void GeometrySelection::seedViewportSelection()
{
    if (!Gui::Application::Instance || _references.empty()) {
        return;
    }
    // Discard whatever the user had selected, then mark the current references as
    // selected so they can be modified in place (picked to add, Ctrl-picked to drop).
    Gui::Selection().clearSelection();
    for (const GeometryReference& ref : _references) {
        App::Document* document = ref.object ? ref.object->getDocument() : nullptr;
        if (!document) {
            continue;
        }
        // An object inside a GeoFeatureGroup (a PartDesign Body, a Part…) is rendered
        // under that group's coordinate node, so the selection must be anchored at the
        // top-level container with a path down to the object. Anchoring at the bare
        // object stores a valid entry that the 3D view cannot match, so nothing
        // highlights. Walk up every enclosing group to build that path.
        App::DocumentObject* anchor = ref.object;
        std::string subName = ref.subName;
        while (App::DocumentObject* group = App::GeoFeatureGroupExtension::getGroupOfObject(anchor)) {
            subName = std::string(anchor->getNameInDocument()) + "." + subName;
            anchor = group;
        }
        Gui::Selection().addSelection(
            document->getName(),
            anchor->getNameInDocument(),
            subName.empty() ? nullptr : subName.c_str()
        );
    }
}

void GeometrySelection::cancelSelecting()
{
    if (!_selecting) {
        return;
    }
    stopSelecting();
    if (_references != _referencesBeforeSelecting) {
        updateReferences(_referencesBeforeSelecting);
    }
}

bool GeometrySelection::appendRequested() const
{
    return _quantity == GeometryQuantity::AllowMultiple
        && (QApplication::keyboardModifiers() & Qt::ControlModifier) != 0;
}

std::optional<GeometryReference> GeometrySelection::referenceFromChange(
    const Gui::SelectionChanges& msg
) const
{
    App::Document* document = App::GetApplication().getDocument(msg.pDocName);
    App::DocumentObject* object = document ? document->getObject(msg.pObjectName) : nullptr;
    if (!object) {
        return std::nullopt;
    }
    return GeometryReference {
        .object = object,
        .subName = msg.pSubName ? std::string(msg.pSubName) : std::string()
    };
}

void GeometrySelection::handleDeselect(const Gui::SelectionChanges& msg)
{
    // Only a multi-value selection lets the user toggle individual references off.
    if (_quantity != GeometryQuantity::AllowMultiple) {
        return;
    }
    std::optional<GeometryReference> removed = referenceFromChange(msg);
    if (!removed) {
        return;
    }
    std::vector<GeometryReference> next = _references;
    auto found = std::ranges::find(next, *removed);
    if (found == next.end()) {
        return;
    }
    next.erase(found);
    updateReferences(std::move(next));
}

void GeometrySelection::scheduleConfirmOnClear()
{
    _confirmOnClearPending = true;
    // Fire after the current synchronous selection burst: a replacing pick emits the
    // clear and its AddSelection back-to-back, and that AddSelection clears the flag
    // below before this runs. A standalone clear (empty-space click) survives to here.
    QTimer::singleShot(0, this, [this] {
        if (_confirmOnClearPending && _selecting) {
            _confirmOnClearPending = false;
            stopSelecting();
        }
    });
}

void GeometrySelection::onSelectionChanged(const Gui::SelectionChanges& msg)
{
    if (!_selecting) {
        return;
    }
    Q_EMIT pickSelectionChanged(msg);
    if (msg.Type == Gui::SelectionChanges::ClrSelection) {
        scheduleConfirmOnClear();
        // The seeded references have just left the 3D selection, so nothing is painting
        // them any more; the Reference role has to take them back over at once rather
        // than wait for the deferred confirm.
        refreshHighlight();
        return;
    }
    // Any other change belongs to an active pick, so cancel a pending empty-space confirm.
    _confirmOnClearPending = false;
    if (msg.Type == Gui::SelectionChanges::RmvSelection) {
        handleDeselect(msg);
        // Whatever the deselect did to the references, one of them is no longer painted
        // by the 3D selection, so what the Reference role owes has changed.
        refreshHighlight();
        return;
    }
    if (msg.Type != Gui::SelectionChanges::AddSelection) {
        return;
    }

    std::optional<GeometryReference> picked = referenceFromChange(msg);
    if (!picked) {
        return;
    }

    const bool append = appendRequested();
    std::vector<GeometryReference> next;
    if (append) {
        next = _references;
        if (std::ranges::find(next, *picked) == next.end()) {
            next.push_back(std::move(*picked));
        }
    }
    else {
        next.push_back(std::move(*picked));
    }
    updateReferences(std::move(next));

    // A replacing pick completes the intent of a single selection, so the
    // session ends; an appending pick (AllowMultiple + modifier) keeps it open.
    if (!append) {
        stopSelecting();
    }
}

void GeometrySelection::bind(App::Property& prop)
{
    unbind();
    _boundProperty = &prop;

    if (auto* container = dynamic_cast<App::DocumentObject*>(prop.getContainer())) {
        _boundContainer = container;
        App::Document* document = container->getDocument();
        _propertyChangedConnection = document->signalChangedObject.connect(
            [this](const App::DocumentObject&, const App::Property& changed) {
                if (&changed == _boundProperty && !_writingBack) {
                    reloadFromProperty();
                }
            }
        );
        // If the owning object is deleted (e.g. an aborted create command), drop the
        // binding at once so the changed-signal handler can never touch a dead property.
        _objectDeletedConnection = document->signalDeletedObject.connect(
            [this](const App::DocumentObject& deleted) {
                if (&deleted == _boundContainer) {
                    _boundProperty = nullptr;
                    _boundContainer = nullptr;
                }
            }
        );
    }
    reloadFromProperty();
}

void GeometrySelection::unbind()
{
    _propertyChangedConnection.disconnect();
    _objectDeletedConnection.disconnect();
    _boundProperty = nullptr;
    _boundContainer = nullptr;
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
            // PropertyLinkSub: single object; collect its sub names. A whole-object
            // reference (empty subName) contributes no sub, so a whole sketch links
            // as (object, {}) — not (object, {""}), which downstream code would try
            // to resolve as a real subelement.
            App::DocumentObject* object = _references.front().object;
            std::vector<std::string> subNames;
            subNames.reserve(_references.size());
            for (const GeometryReference& ref : _references) {
                if (!ref.subName.empty()) {
                    subNames.push_back(ref.subName);
                }
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
