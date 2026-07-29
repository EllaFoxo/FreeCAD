// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2026 Kacper Donat <kacper@kadet.net>                     *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#pragma once

#include <vector>

#include <QIcon>
#include <QMargins>
#include <QRect>
#include <QString>
#include <QVariant>
#include <QWidget>

#include <FCGlobal.h>

#include "GeometrySelection.h"

class QVBoxLayout;

namespace Gui
{

/**
 * One predefined choice for the selector's combo mode: an icon and label to show,
 * the geometry references it stands for (empty for a logical option that carries no
 * geometry, e.g. "Document origin"), and optional user data surfaced via currentData().
 */
struct GuiExport GeometrySelectorOption
{
    QIcon icon;
    QString label;
    std::vector<GeometryReference> references;
    QVariant userData;

    /// One option standing for a single reference, its icon and label derived from the
    /// object's view provider exactly like the reference rows (label only, when headless).
    static GeometrySelectorOption fromReference(const GeometryReference& reference);
    /// One option standing for a whole group of references; icon and label are taken from
    /// the first reference.
    static GeometrySelectorOption fromReferences(std::vector<GeometryReference> references);
    /// The managed "Custom…" entry that turns the widget back into a free-pick Select Box.
    static GeometrySelectorOption customEntry();
};

/**
 * Composite view widget that owns a GeometrySelection core and renders it
 * as a list-styled frame. Exposes the core via selection() so callers can
 * configure gate, binding, and preview hooks.
 *
 * The frame is painted via the ambient QStyle, and all spacing comes from the
 * design-system tokens resolved through the List token chain, so any ambient
 * QStyle themes it correctly — no hard dependency on FreeCADStyle.
 *
 * A filled reference presents its icon and name at rest; each row reveals its
 * own remove control only while the pointer is over that row, matching the
 * Figma design.
 */
class GuiExport GeometrySelectorWidget: public QWidget
{
    Q_OBJECT
    Q_PROPERTY(Gui::GeometryQuantity quantity READ quantity WRITE setQuantity)

public:
    explicit GeometrySelectorWidget(GeometryQuantity mode, QWidget* parent = nullptr);

    explicit GeometrySelectorWidget(QWidget* parent = nullptr);

    /// The owned core; callers use this to configure gate, binding, etc.
    GeometrySelection* selection() const
    {
        return m_selection;
    }

    /// The selection quantity mode (Single / AllowMultiple); delegates to the core.
    GeometryQuantity quantity() const;
    void setQuantity(GeometryQuantity mode);

    /// Predefined options: a non-empty list turns the widget into a combo (Select Box).
    /// An empty list restores the free-pick behaviour. Reconciles the current index against
    /// the current references (reverse match).
    void setOptions(std::vector<GeometrySelectorOption> options);
    void addOption(GeometrySelectorOption option);
    /// Opt-in managed "Custom…" entry that turns the combo back into a free-pick Select Box.
    void setAllowCustom(bool on);
    /// True while a non-empty options list is set.
    bool isComboMode() const;

    /// QComboBox-like read-back. currentIndex() is -1 when nothing is current; the Custom
    /// entry (when enabled) is the last index, == the number of predefined options.
    int currentIndex() const;
    void setCurrentIndex(int index);
    QVariant currentData() const;
    QString currentText() const;
    /// The current predefined option, or nullptr at the Custom index / when nothing is current.
    const GeometrySelectorOption* currentOption() const;

    /// The three rendered states, derived from references + session.
    enum class VisualState
    {
        Empty,          // idle, no references
        Selecting,      // in a selection session: horizontal prompt chrome over a dimming backdrop
        ReferenceList,  // idle with ≥1 references (capped scroll list)
    };

    /// Classifies the current state from the core alone; independent of any QStyle or
    /// Gui::Application, so it is well-defined even in a headless harness.
    VisualState visualState() const;

Q_SIGNALS:
    void currentIndexChanged(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;
    // In the empty state a click anywhere on the frame starts selecting, so the prompt is
    // a plain placeholder label rather than a button.
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private Q_SLOTS:
    void rebuildRows();

private:
    QWidget* makeEmptyRow();
    /// A capped scroll list with one row per reference, each row revealing its own
    /// remove control on hover.
    QWidget* makeReferenceList();
    /// The selection-session chrome: a horizontal placeholder + Cancel (and Done for
    /// multi-select) row over a dimming backdrop, above the committed references when any.
    QWidget* makeSelecting();

    void clearRows();

    /// Highlights the given reference's geometry in the 3D view, reusing the view's
    /// preselection mechanism so it matches a live cursor highlight.
    void previewReferenceInView(const GeometryReference& reference);
    /// Clears any 3D-view highlight this widget requested on hover.
    void clearReferencePreview();

    /// Resolves layout margins, spacing and fixed height from style tokens.
    void applyStyleMetrics();
    /// One row's height: its icon/label content plus the resolved item vertical padding.
    int rowHeight() const;
    /// The reference list's rendered height: rows up to the visible-row cap.
    int referenceListHeight() const;

    /// The Custom entry's index (== number of predefined options) when Custom is enabled,
    /// otherwise -1.
    int customIndex() const;
    /// True when @p index is the managed Custom entry (only meaningful when Custom is enabled).
    bool isCustomIndex(int index) const;
    /// Recomputes currentIndex from the current references after an external change (R3).
    void reconcileIndexFromReferences();
    /// True while setCurrentIndex is applying references, so the referencesChanged reaction
    /// does not clobber the just-set index.
    bool m_applyingChoice = false;

    /// Primary activation for a click on the frame/rows/prompt: opens the options popup in
    /// combo mode, otherwise starts a free-pick session (the pre-combo behaviour).
    void activatePrimary();
    /// Shows the dropdown popup under the control (combo mode only).
    void openOptionsPopup();
    /// Applies the popup's choice: sets the current index, or re-starts a pick for Custom.
    void onOptionActivated(int index);
    /// The strip on the right of the frame in which the chevron is painted (combo mode).
    QRect chevronRect() const;

    GeometrySelection* m_selection;
    QVBoxLayout* m_contentLayout;
    /// Per-row inset, resolved from the ListItemPadding token; the frame itself is flush.
    QMargins m_itemPadding {6, 4, 6, 4};
    /// Icon-to-label spacing within a row, resolved from the ListItemIconSpacing token.
    int m_itemSpacing = 6;
    /// Total height of one line of the control, frame included: a single-value selector and one
    /// row of a multi-value one are exactly this tall, matching sibling form fields. Resolved
    /// from the GeometrySelector box geometry (GeometrySelectorMinHeight); 0 until resolved, when
    /// the row falls back to its content height (headless harness with no FreeCADStyle).
    int m_lineHeight = 0;
    /// Frame border thickness resolved from the GeometrySelector box style; the outer layout
    /// insets content by it so one row plus the frame equals one line height.
    int m_frameThickness = 1;
    /// Vertical gap between reference rows, resolved from ListItemSpacing (Item/Spacing). 0
    /// until resolved (headless fallback), where rows abut like the current behaviour.
    int m_rowSpacing = 0;
    /// Container inner padding, resolved from the ListPadding token. Follows the border-box
    /// model: one line stays m_lineHeight tall, so this padding eats into the row content
    /// rather than expanding the control. {0,0,0,0} until resolved (frame flush).
    QMargins m_containerPadding;

    /// Predefined combo options (excludes the managed Custom entry). Empty ⇒ free-pick mode.
    std::vector<GeometrySelectorOption> m_options;
    /// The current combo index; -1 when nothing is current. Authoritative for user choices.
    int m_currentIndex = -1;
    /// Whether the managed Custom entry is appended as the last option.
    bool m_allowCustom = false;
};

}  // namespace Gui
