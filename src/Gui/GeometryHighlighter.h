// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <array>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include <QObject>
#include <fastsignals/signal.h>

#include <App/DocumentObserver.h>
#include <App/PropertyOverrides.h>
#include <App/PropertyStandard.h>
#include <FCGlobal.h>
#include <Gui/GeometryReference.h>

namespace App
{
class Document;
class DocumentObject;
}  // namespace App

namespace Gui
{

/**
 * Which references each highlight role should render.
 *
 * Holds one set of references per role and answers, for any role, the subset it
 * still owns once the more specific roles have claimed theirs. Contains no Coin
 * or view code, so it can be exercised without a 3D view.
 */
class GuiExport GeometryHighlightModel
{
public:
    /// Replaces everything held under @p role.
    void setHighlighted(HighlightRole role, std::vector<GeometryReference> references);
    void clear(HighlightRole role);
    void clear();

    /// The references @p role should actually render.
    std::vector<GeometryReference> effective(HighlightRole role) const;

    /// Forgets every reference to @p object.
    void dropObject(const App::DocumentObject* object);
    /// Forgets every reference to an object owned by @p document.
    void dropDocument(const App::Document* document);

private:
    std::vector<GeometryReference>& slot(HighlightRole role);
    const std::vector<GeometryReference>& slot(HighlightRole role) const;

    std::array<std::vector<GeometryReference>, highlightRoleCount> _byRole;
};

/**
 * Keeps a set of objects revealed for exactly as long as someone needs them.
 *
 * The whole revealed set is declared in one call rather than adjusted object by
 * object, so whatever drops out of it is restored in the same breath. Every reveal
 * is an RAII property override, so restoration happens on every path out —
 * including destruction — and cannot be forgotten. An object already visible is
 * tracked like any other but never written to: it needs nothing revealed and
 * nothing put back, and leaving it alone spares the document a change it would
 * otherwise have to react to.
 */
class GuiExport HighlightVisibility
{
public:
    HighlightVisibility() = default;

    FC_DISABLE_COPY_MOVE(HighlightVisibility)

    /// Reveals every object among @p objects and restores every one this revealed
    /// earlier that @p objects no longer lists.
    void setRevealed(const std::vector<App::DocumentObject*>& objects);

private:
    /// Holds one object visible and puts back the visibility it found, on
    /// destruction, whenever that happens.
    class Override
    {
    public:
        explicit Override(App::DocumentObject* object);
        ~Override();

        FC_DISABLE_COPY_MOVE(Override)

    private:
        /// Held weakly: an object deleted meanwhile resolves to null, and the
        /// override is then abandoned rather than written back through a property
        /// that died with it.
        App::DocumentObjectWeakPtrT _object;
        /// Empty for an object that was already visible, which owns no override and
        /// restores nothing.
        std::optional<App::ScopedPropertyOverride<App::PropertyBool>> _visible;
    };

    /// One entry per object this revealed; erasing an entry restores it. The key is
    /// only ever compared, never dereferenced, so one belonging to a deleted object
    /// does no harm.
    std::map<App::DocumentObject*, Override> _revealed;
};

/**
 * Renders a set of geometry references on top in the 3D view.
 *
 * Instantiable and independent: several highlighters can be live at once, and
 * none of them touches the application selection. Highlighting a reference
 * never rebuilds geometry — it re-renders the nodes already in the scene.
 *
 * A hidden object is revealed only while the highlight on it is transient — a
 * hover, or a reference drawn during a pick session — and is returned to
 * whatever visibility it had the moment that ends. Highlighting alone, with
 * neither of those in force, changes nobody's visibility.
 */
class GuiExport GeometryHighlighter: public QObject
{
    Q_OBJECT

public:
    explicit GeometryHighlighter(QObject* parent = nullptr);
    ~GeometryHighlighter() override;

    /// Replaces everything highlighted under @p role.
    void setHighlighted(HighlightRole role, std::vector<GeometryReference> references);
    void clear(HighlightRole role);
    void clear();

    /// Declares whether a pick session is running, which is what makes the reference
    /// highlights transient enough to reveal a hidden object for.
    void setSelecting(bool selecting);
    bool isSelecting() const
    {
        return _selecting;
    }

    const GeometryHighlightModel& model() const
    {
        return _model;
    }

private:
    /// Rebuilds both roles in every 3D view of every document holding a reference.
    void refresh();
    /// Withdraws this highlighter's annotations from every view it drew in last time
    /// as well as every view it is about to draw in, and adopts @p documents as the
    /// new set. Nobody else's annotations are affected.
    void withdrawAndAdopt(std::set<App::Document*> documents);

    GeometryHighlightModel _model;
    /// The documents whose views currently hold this highlighter's annotations.
    std::set<App::Document*> _touchedDocuments;
    /// Whether a pick session is running; see setSelecting().
    bool _selecting = false;
    /// Declared afresh on every refresh(), so it always mirrors what is drawn in a
    /// transient role.
    HighlightVisibility _visibility;
    fastsignals::scoped_connection _objectDeletedConnection;
    fastsignals::scoped_connection _documentDeletedConnection;
};

}  // namespace Gui
