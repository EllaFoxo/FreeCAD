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

#include "GeometrySelectorPopup.h"

#include <functional>

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>

#include "Application.h"
#include "ElideLabel.h"
#include "FreeCADStyle.h"
#include "IconManager.h"

using namespace Gui;

namespace
{
constexpr int IconSize = 16;
constexpr QMargins RowPadding {6, 4, 6, 4};
constexpr int RowSpacing = 6;

/// One selectable option row: icon + label, an optional trailing check for the current row,
/// a hovered background painted through the shared List box painting, and a click callback.
class OptionRow: public QWidget
{
public:
    OptionRow(
        const GeometrySelectorOption& option,
        bool isCurrent,
        std::function<void()> onActivate,
        std::function<void()> onHover,
        QWidget* parent
    )
        : QWidget(parent)
        , m_activate(std::move(onActivate))
        , m_hover(std::move(onHover))
    {
        setProperty("component", "List");
        auto* rowLayout = new QHBoxLayout(this);
        rowLayout->setContentsMargins(RowPadding);
        rowLayout->setSpacing(RowSpacing);

        if (!option.icon.isNull()) {
            auto* iconLabel = new QLabel(this);
            iconLabel->setPixmap(option.icon.pixmap(IconSize, IconSize));
            rowLayout->addWidget(iconLabel);
        }

        auto* label = new ElideLabel(this);
        label->setText(option.label);
        label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        rowLayout->addWidget(label, 1);

        if (isCurrent) {
            auto* check = new QLabel(this);
            check->setObjectName(QStringLiteral("gsw_option_check"));
            check->setPixmap(
                IconManager::instance()
                    .icon(QStringLiteral(":/icons/tabler/outline/check.svg"))
                    .pixmap(IconSize, IconSize)
            );
            rowLayout->addWidget(check);
        }
    }

    void setHighlighted(bool on)
    {
        if (m_highlighted != on) {
            m_highlighted = on;
            update();
        }
    }

protected:
    void enterEvent(QEnterEvent* /*event*/) override
    {
        if (m_hover) {
            m_hover();
        }
    }
    void paintEvent(QPaintEvent* /*event*/) override
    {
        if (!m_highlighted || !Application::Instance) {
            return;
        }
        StyleParameters::StyleContext context = FreeCADStyle::contextOf(this);
        context.element = StyleParameters::StyleComponentElement::Row;
        context.state |= StyleParameters::StyleState::Hovered;
        QPainter painter(this);
        Application::Instance->freeCADStyle()->paintBox(&painter, rect(), context);
    }
    void mousePressEvent(QMouseEvent* event) override
    {
        // Accept the press so the matching release is delivered to this row; the child
        // labels do not accept events, so without this the release could be lost.
        if (event->button() == Qt::LeftButton) {
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && m_activate) {
            m_activate();
        }
    }

private:
    std::function<void()> m_activate;
    std::function<void()> m_hover;
    bool m_highlighted = false;
};
}  // namespace

GeometrySelectorPopup::GeometrySelectorPopup(
    std::vector<GeometrySelectorOption> options,
    bool allowCustom,
    int currentIndex,
    QWidget* parent
)
    : QWidget(parent, Qt::Popup)
    , m_options(std::move(options))
    , m_allowCustom(allowCustom)
    , m_currentIndex(currentIndex)
{
    setObjectName(QStringLiteral("gsw_options_popup"));
    if (Application::Instance) {
        setStyle(Application::Instance->freeCADStyle());
    }

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    m_rowsLayout = new QVBoxLayout;
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(0);
    outerLayout->addLayout(m_rowsLayout);

    buildRows();
}

int GeometrySelectorPopup::optionCount() const
{
    return static_cast<int>(m_options.size()) + (m_allowCustom ? 1 : 0);
}

void GeometrySelectorPopup::buildRows()
{
    const auto addRow = [this](const GeometrySelectorOption& option, int index, const QString& name) {
        auto* row = new OptionRow(
            option,
            /*isCurrent=*/index == m_currentIndex,
            [this, index] { activateIndex(index); },
            [this, index] { setHighlight(index); },
            this
        );
        row->setObjectName(name);
        m_rowsLayout->addWidget(row);
        m_rows.push_back(row);
    };

    for (std::size_t index = 0; index < m_options.size(); ++index) {
        addRow(m_options[index], static_cast<int>(index), QStringLiteral("gsw_option_row"));
    }
    if (m_allowCustom) {
        addRow(
            GeometrySelectorOption::customEntry(),
            static_cast<int>(m_options.size()),
            QStringLiteral("gsw_option_custom")
        );
    }
}

void GeometrySelectorPopup::setHighlight(int index)
{
    if (index < 0 || index >= static_cast<int>(m_rows.size())) {
        return;
    }
    if (m_highlight >= 0 && m_highlight < static_cast<int>(m_rows.size())) {
        static_cast<OptionRow*>(m_rows[m_highlight])->setHighlighted(false);
    }
    m_highlight = index;
    static_cast<OptionRow*>(m_rows[index])->setHighlighted(true);
}

void GeometrySelectorPopup::moveHighlight(int delta)
{
    const int count = optionCount();
    if (count == 0) {
        return;
    }
    int start = m_highlight;
    if (start < 0) {
        start = (delta > 0) ? -1 : count;
    }
    setHighlight((start + delta + count) % count);
}

void GeometrySelectorPopup::activateIndex(int index)
{
    if (index < 0 || index >= optionCount()) {
        return;
    }
    Q_EMIT optionActivated(index);
}

void GeometrySelectorPopup::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
        case Qt::Key_Down:
            moveHighlight(1);
            return;
        case Qt::Key_Up:
            moveHighlight(-1);
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (m_highlight >= 0) {
                activateIndex(m_highlight);
            }
            return;
        case Qt::Key_Escape:
            close();
            return;
        default:
            QWidget::keyPressEvent(event);
    }
}
