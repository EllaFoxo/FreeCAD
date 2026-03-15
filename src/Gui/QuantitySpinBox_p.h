/***************************************************************************
 *   Copyright (c) 2015 Eivind Kvedalen <eivind@kvedalen.name>             *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 51 Franklin Street,      *
 *   Fifth Floor, Boston, MA  02110-1301, USA                              *
 *                                                                         *
 ***************************************************************************/

#pragma once

#include <QLineEdit>
#include <QToolButton>

#include <Gui/Application.h>
#include <Gui/StyleParameters.h>

class ExpressionButton: public QToolButton
{
    Q_OBJECT
public:
    explicit ExpressionButton(QWidget* parent)
        : QToolButton(parent)
    {
        using namespace Gui::StyleParameters;

        setAutoRaise(true);
        setCheckable(true);
        setFocusPolicy(Qt::NoFocus);
        setProperty("component", "ExpressionButton");

        int size = static_cast<int>(
            Gui::Application::Instance->styleParameterManager()->resolve(ExpressionButtonSize).value
        );

        setFixedSize(size, size);
    }

    void setExpressionText(const QString& text)
    {
        if (text.isEmpty()) {
            setToolTip(genericExpressionEditorTooltip);
        }
        else {
            setToolTip(expressionEditorTooltipPrefix + text);
        }
    }

    void show()
    {
        if (auto* parentLineEdit = qobject_cast<QLineEdit*>(parent())) {
            QMargins margins = parentLineEdit->contentsMargins();
            margins.setRight(2 * margins.right() + sizeHint().width());
            parentLineEdit->setContentsMargins(margins);
        }
        QToolButton::show();
    }

    void setNormalIcon(const QIcon& icon)
    {
        normalIcon_ = icon;
        setIcon(icon);
    }

    void restoreNormalIcon()
    {
        setIcon(normalIcon_);
    }

protected:
    // Prevent Qt from auto-toggling the checked state on click.
    // The checked state is driven exclusively by the expression binding
    // via setChecked(), so clicking must not change it on its own.
    void nextCheckState() override
    {}

private:
    QIcon normalIcon_;
    const QString genericExpressionEditorTooltip = tr("Enter expression… (=)");
    const QString expressionEditorTooltipPrefix = tr("Expression:") + QStringLiteral(" ");
};
