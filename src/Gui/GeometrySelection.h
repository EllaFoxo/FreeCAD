// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <QObject>

#include <fastsignals/signal.h>

#include <FCGlobal.h>
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

    void startSelecting();
    void stopSelecting();
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

    std::vector<GeometryReference> _references;
    GeometryQuantity _quantity;

private:
    GateFactory _gateFactory;
    bool _selecting = false;

    App::Property* _boundProperty = nullptr;
    bool _autoApply = true;
    bool _writingBack = false;
    fastsignals::scoped_connection _propertyChangedConnection;

    void reloadFromProperty();
    bool writeToProperty();
};

}  // namespace Gui
