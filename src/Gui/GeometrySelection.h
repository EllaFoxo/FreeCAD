// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <QObject>

#include <fastsignals/signal.h>

#include <App/DocumentObserver.h>
#include <FCGlobal.h>
#include <Gui/GeometrySelectionGate.h>
#include <Gui/Selection/Selection.h>

class QString;

namespace App
{
class Document;
class DocumentObject;
class Property;
}  // namespace App

namespace Gui
{
class SelectionGate;
}

namespace Gui
{

enum class GeometryQuantity
{
    Single,         // exactly one reference
    AllowMultiple,  // intent is one; Ctrl-pick forces more
    // Multiple is a separate future widget that reuses this core.
};

/// One picked reference: a whole object (empty subName) or one of its subelements.
struct GuiExport GeometryReference
{
    App::DocumentObject* object = nullptr;
    std::string subName;

    bool operator==(const GeometryReference& other) const
    {
        return object == other.object && subName == other.subName;
    }
};

/**
 * Widget-agnostic core of the geometry selector. Owns the selected-reference
 * model and quantity mode; later tasks add the selection session, gate, and
 * property binding. Emits referencesChanged() whenever the model changes.
 */
class GuiExport GeometrySelection: public QObject, public Gui::SelectionObserver
{
    Q_OBJECT

public:
    explicit GeometrySelection(
        GeometryQuantity mode = GeometryQuantity::Single,
        QObject* parent = nullptr
    );
    ~GeometrySelection() override;

    GeometryQuantity quantity() const
    {
        return _quantity;
    }
    void setQuantity(GeometryQuantity mode);

    const std::vector<GeometryReference>& references() const
    {
        return _references;
    }
    void setReferences(std::vector<GeometryReference> references);
    void removeReference(std::size_t index);
    void clear();

    using GateFactory = std::function<std::unique_ptr<Gui::SelectionGate>()>;

    void setSelectionGate(GateFactory factory);
    void setSelectionFilter(const QString& filter);
    void setAllowedKinds(GeometryKinds kinds, App::DocumentObject* support = nullptr);

    void startSelecting();
    void stopSelecting();
    /// Ends the session and restores the references held when it began, so a
    /// cancelled pick keeps the previous selection (or stays empty).
    void cancelSelecting();
    bool isSelecting() const
    {
        return _selecting;
    }

    void bind(App::Property& prop);
    void unbind();
    bool isBound() const
    {
        return _boundProperty != nullptr;
    }
    void setAutoApply(bool on)
    {
        _autoApply = on;
    }
    bool autoApply() const
    {
        return _autoApply;
    }
    bool apply();

Q_SIGNALS:
    void referencesChanged();
    void selectionModeEntered();
    void selectionModeExited();

protected:
    // Single place every model mutation routes through, so later tasks (binding)
    // can react in one spot.
    void updateReferences(std::vector<GeometryReference> references);

    void onSelectionChanged(const Gui::SelectionChanges& msg) override;
    // Whether the current pick should append (AllowMultiple + modifier) vs replace.
    virtual bool appendRequested() const;
    // Resolves the picked object/subelement of a change into a reference, or
    // nullopt if the object no longer exists.
    std::optional<GeometryReference> referenceFromChange(const Gui::SelectionChanges& msg) const;

    std::vector<GeometryReference> _references;
    GeometryQuantity _quantity;

private:
    GateFactory _gateFactory;
    bool _selecting = false;
    /// References captured at startSelecting(), restored by cancelSelecting().
    std::vector<GeometryReference> _referencesBeforeSelecting;

    /// One object whose visibility we forced on for a session. Tracked weakly so
    /// restoration is skipped for any object deleted meanwhile (e.g. by an aborted
    /// command), which is what keeps teardown from touching freed view providers.
    struct ForcedVisibility
    {
        App::DocumentObjectWeakPtrT object;
        bool restoreTo = false;
    };
    std::vector<ForcedVisibility> _forcedVisibility;

    /// Shows referenced objects that are hidden so the user can see the current
    /// selection while picking; the originals are restored by restoreReferencedObjects().
    void showReferencedObjects();
    void restoreReferencedObjects();
    /// Mirrors the current references into the 3D selection so they appear picked
    /// and can be toggled off with Ctrl (AllowMultiple only).
    void seedViewportSelection();
    /// Drops the reference matching a Ctrl-deselect in the 3D view (AllowMultiple only).
    void handleDeselect(const Gui::SelectionChanges& msg);

    App::Property* _boundProperty = nullptr;
    /// The bound property's owning object; watched so a deletion drops the binding
    /// before any stale property access can happen.
    App::DocumentObject* _boundContainer = nullptr;
    bool _autoApply = true;
    bool _writingBack = false;
    fastsignals::scoped_connection _propertyChangedConnection;
    fastsignals::scoped_connection _objectDeletedConnection;

    void reloadFromProperty();
    bool writeToProperty();
};

}  // namespace Gui

// Enables use as a Q_PROPERTY type (QVariant / setProperty on the selector widget).
Q_DECLARE_METATYPE(Gui::GeometryQuantity)
