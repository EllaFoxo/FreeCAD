// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2024 Kacper Donat <kacper@kadet.net>                    *
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
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include <FCConfig.h>

#ifdef FC_OS_MACOSX
# include <OpenGL/gl.h>
#else
# ifdef FC_OS_WIN32
#  include <windows.h>
# endif
# include <GL/gl.h>
#endif

#include <Inventor/elements/SoCacheElement.h>
#include <algorithm>

#include "So3DAnnotation.h"
#include <Gui/Selection/Selection.h>

using namespace Gui;

SO_ELEMENT_SOURCE(SoDelayedAnnotationsElement);

bool SoDelayedAnnotationsElement::isProcessingDelayedPaths = false;

void SoDelayedAnnotationsElement::init(SoState* state)
{
    SoElement::init(state);
    paths.clear();
}

void SoDelayedAnnotationsElement::initClass()
{
    SO_ELEMENT_INIT_CLASS(SoDelayedAnnotationsElement, inherited);

    SO_ENABLE(SoGLRenderAction, SoDelayedAnnotationsElement);
}

SoDelayedAnnotationsElement* SoDelayedAnnotationsElement::getElement(SoState* state)
{
    return static_cast<SoDelayedAnnotationsElement*>(state->getElementNoPush(classStackIndex));
}

void SoDelayedAnnotationsElement::addDelayedPath(SoState* state, SoPath* path, int priority)
{
    // add to unified storage with specified priority (default = 0)
    getElement(state)->paths.emplace_back(path, priority);
}

bool SoDelayedAnnotationsElement::hasDelayedPaths(SoState* state)
{
    return !getElement(state)->paths.empty();
}

void SoDelayedAnnotationsElement::processDelayedPathsWithPriority(SoState* state, SoGLRenderAction* action)
{
    auto elt = static_cast<SoDelayedAnnotationsElement*>(state->getElementNoPush(classStackIndex));

    if (elt->paths.empty()) {
        return;
    }

    std::stable_sort(
        elt->paths.begin(),
        elt->paths.end(),
        [](const PriorityPath& first, const PriorityPath& second) {
            return first.priority < second.priority;
        }
    );

    // Take ownership before replaying: each apply below re-enters the render
    // traversal, which is free to reach this element again. Iterating the member
    // vector across that would be iterating a container someone else may append to.
    std::vector<PriorityPath> ordered;
    ordered.swap(elt->paths);

    isProcessingDelayedPaths = true;

    // One apply per layer, not per path. Each apply runs its own nested delayed-path
    // phase on the way out, and that phase is where a nested SoFCPathAnnotation draws;
    // finishing a layer before starting the next is what keeps the layers ordered.
    for (auto layerBegin = ordered.begin(); layerBegin != ordered.end();) {
        const int currentLayer = layerBegin->priority;
        const auto layerEnd
            = std::find_if(layerBegin, ordered.end(), [currentLayer](const PriorityPath& candidate) {
                  return candidate.priority != currentLayer;
              });

        SoPathList batch;
        for (auto entry = layerBegin; entry != layerEnd; ++entry) {
            batch.append(entry->path);
        }

        action->apply(batch, TRUE);

        layerBegin = layerEnd;
    }

    isProcessingDelayedPaths = false;
}

SO_NODE_SOURCE(So3DAnnotation);

bool So3DAnnotation::render = false;

So3DAnnotation::So3DAnnotation()
{
    SO_NODE_CONSTRUCTOR(So3DAnnotation);

    SO_NODE_ADD_FIELD(layer, (static_cast<int>(AnnotationLayer::Overlay)));
}

void So3DAnnotation::initClass()
{
    SO_NODE_INIT_CLASS(So3DAnnotation, SoSeparator, "3DAnnotation");
}

void So3DAnnotation::GLRender(SoGLRenderAction* action)
{
    switch (action->getCurPathCode()) {
        case SoAction::NO_PATH:
        case SoAction::BELOW_PATH:
            this->GLRenderBelowPath(action);
            break;
        case SoAction::OFF_PATH:
            // do nothing. Separator will reset state.
            break;
        case SoAction::IN_PATH:
            this->GLRenderInPath(action);
            break;
    }
}

void So3DAnnotation::GLRenderBelowPath(SoGLRenderAction* action)
{
    if (render) {
        inherited::GLRenderBelowPath(action);
    }
    else {
        SoCacheElement::invalidate(action->getState());
        SoDelayedAnnotationsElement::addDelayedPath(
            action->getState(),
            action->getCurPath()->copy(),
            layer.getValue()
        );
    }
}

void So3DAnnotation::GLRenderInPath(SoGLRenderAction* action)
{
    if (render) {
        inherited::GLRenderInPath(action);
    }
    else {
        SoCacheElement::invalidate(action->getState());
        SoDelayedAnnotationsElement::addDelayedPath(
            action->getState(),
            action->getCurPath()->copy(),
            layer.getValue()
        );
    }
}

void So3DAnnotation::GLRenderOffPath(SoGLRenderAction* /* action */)
{
    // should never render, this is a separator node
}
