/****************************************************************************
 *   Copyright (c) 2022 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/


#include <Inventor/details/SoDetail.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoPickStyle.h>
#include <Inventor/nodes/SoSeparator.h>


#include "Application.h"
#include "Document.h"
#include "SoFCUnifiedSelection.h"
#include "Utilities.h"
#include "View3DInventorSelection.h"
#include "ViewProviderDocumentObject.h"
#include <App/Document.h>
#include <App/GeoFeature.h>
#include <App/GeoFeatureGroupExtension.h>
#include <Base/Console.h>

FC_LOG_LEVEL_INIT("3DViewerSelection", true, true)

using namespace Gui;

namespace
{
/// The scene-graph node name a highlight role's subgroup carries, for the sake of
/// anyone reading the graph in the scene inspector.
const char* highlightRoleName(HighlightRole role)
{
    switch (role) {
        case HighlightRole::Reference:
            return "HighlightReference";
        case HighlightRole::Hovered:
            return "HighlightHovered";
        case HighlightRole::COUNT:
            break;
    }
    return "Highlight";
}
}  // namespace

View3DInventorSelection::View3DInventorSelection(SoFCUnifiedSelection* root)
    : selectionRoot(root)
{
    selectionRoot->ref();

    pcGroupOnTop = new SoSeparator;
    pcGroupOnTop->ref();
    pcGroupOnTop->setName("GroupOnTop");
    root->addChild(pcGroupOnTop);

    auto pcGroupOnTopPickStyle = new SoPickStyle;
    pcGroupOnTopPickStyle->style = SoPickStyle::UNPICKABLE;
    pcGroupOnTopPickStyle->setOverride(true);
    pcGroupOnTopPickStyle->setName("GroupOnTopPickStyle");
    pcGroupOnTop->addChild(pcGroupOnTopPickStyle);

    coin_setenv("COIN_SEPARATE_DIFFUSE_TRANSPARENCY_OVERRIDE", "1", TRUE);
    auto pcGroupOnTopMaterial = new SoMaterial;
    pcGroupOnTopMaterial->transparency = 0.5;
    pcGroupOnTopMaterial->diffuseColor.setIgnored(true);
    pcGroupOnTopMaterial->setOverride(true);
    pcGroupOnTopMaterial->setName("GroupOnTopMaterial");
    pcGroupOnTop->addChild(pcGroupOnTopMaterial);

    {
        auto selRoot = new SoFCSelectionRoot;
        selRoot->selectionStyle = SoFCSelectionRoot::PassThrough;
        pcGroupOnTopSel = selRoot;
        pcGroupOnTopSel->setName("GroupOnTopSel");
        pcGroupOnTopSel->ref();
        pcGroupOnTop->addChild(pcGroupOnTopSel);
    }

    {
        auto selRoot = new SoFCSelectionRoot;
        selRoot->selectionStyle = SoFCSelectionRoot::PassThrough;
        pcGroupOnTopPreSel = selRoot;
        pcGroupOnTopPreSel->setName("GroupOnTopPreSel");
        pcGroupOnTopPreSel->ref();
        pcGroupOnTop->addChild(pcGroupOnTopPreSel);
    }

    // A sibling of GroupOnTop rather than a child: GroupOnTopMaterial forces
    // transparency 0.5 with override, which would wash out every highlight. Added
    // after it so a reference highlight draws over an ordinary selection highlight.
    pcGroupHighlight = new SoSeparator;
    pcGroupHighlight->ref();
    pcGroupHighlight->setName("GroupHighlight");
    root->addChild(pcGroupHighlight);

    auto pcHighlightPickStyle = new SoPickStyle;
    pcHighlightPickStyle->style = SoPickStyle::UNPICKABLE;
    pcHighlightPickStyle->setOverride(true);
    pcHighlightPickStyle->setName("GroupHighlightPickStyle");
    pcGroupHighlight->addChild(pcHighlightPickStyle);

    const auto makeRoleGroup = [this](const char* name, HighlightRoleNodes& role) {
        auto selRoot = new SoFCSelectionRoot;
        selRoot->selectionStyle = SoFCSelectionRoot::PassThrough;
        role.group = selRoot;
        role.group->setName(name);
        role.group->ref();

        role.style = new SoDrawStyle;
        // Only the line width is ours to force. An override applies to every field
        // that is not ignored, which would otherwise also pin the replayed geometry
        // to FILLED, a zero point size and a solid line pattern — a vertex reference
        // would lose its point size and a dashed view provider its pattern.
        role.style->style.setIgnored(true);
        role.style->pointSize.setIgnored(true);
        role.style->linePattern.setIgnored(true);
        role.style->setOverride(true);
        role.group->addChild(role.style);

        pcGroupHighlight->addChild(role.group);
    };

    // Every slot, not the two named ones: a role added later that was missed here
    // would leave a null group for the destructor to unref.
    for (std::size_t index = 0; index < highlightRoleCount; ++index) {
        makeRoleGroup(highlightRoleName(static_cast<HighlightRole>(index)), highlightRoles.at(index));
    }
}

View3DInventorSelection::~View3DInventorSelection()
{
    selectionRoot->unref();
    pcGroupOnTop->unref();
    pcGroupOnTopPreSel->unref();
    pcGroupOnTopSel->unref();
    pcGroupHighlight->unref();
    for (HighlightRoleNodes& role : highlightRoles) {
        role.group->unref();
    }
}

void View3DInventorSelection::checkGroupOnTop(const SelectionChanges& Reason)
{
    if (Reason.Type == SelectionChanges::SetSelection
        || Reason.Type == SelectionChanges::ClrSelection) {
        clearGroupOnTop();
        if (Reason.Type == SelectionChanges::ClrSelection) {
            return;
        }
    }
    if (Reason.Type == SelectionChanges::RmvPreselect
        || Reason.Type == SelectionChanges::RmvPreselectSignal) {
        SoSelectionElementAction action(SoSelectionElementAction::None, true);
        action.apply(pcGroupOnTopPreSel);
        coinRemoveAllChildren(pcGroupOnTopPreSel);
        objectsOnTopPreSel.clear();
        return;
    }
    if (!getDocument() || !Reason.pDocName || !Reason.pDocName[0] || !Reason.pObjectName) {
        return;
    }
    auto obj = getDocument()->getDocument()->getObject(Reason.pObjectName);
    if (!obj || !obj->isAttachedToDocument()) {
        return;
    }
    std::string key(obj->getNameInDocument());
    key += '.';
    auto subname = Reason.pSubName;
    App::ElementNamePair element;
    App::GeoFeature::resolveElement(obj, Reason.pSubName, element);
    if (Data::isMappedElement(subname) && !element.oldName.empty()) {  // If we have a shortened
                                                                       // element name
        subname = element.oldName.c_str();                             // use if
    }
    if (subname) {
        key += subname;
    }
    if (Reason.Type == SelectionChanges::RmvSelection) {
        auto& objs = objectsOnTop;
        auto pcGroup = pcGroupOnTopSel;
        auto it = objs.find(key.c_str());
        if (it == objs.end()) {
            return;
        }
        int index = pcGroup->findChild(it->second);
        if (index >= 0) {
            auto node = static_cast<SoFCPathAnnotation*>(it->second);
            SoSelectionElementAction action(
                node->getDetail() ? SoSelectionElementAction::Remove : SoSelectionElementAction::None,
                true
            );
            auto path = node->getPath();
            SoTempPath tmpPath(2 + (path ? path->getLength() : 0));
            tmpPath.ref();
            tmpPath.append(pcGroup);
            tmpPath.append(node);
            tmpPath.append(node->getPath());
            action.setElement(node->getDetail());
            action.apply(&tmpPath);
            tmpPath.unrefNoDelete();
            pcGroup->removeChild(index);
            FC_LOG("remove annotation " << Reason.Type << " " << key);
        }
        else {
            FC_LOG("remove annotation object " << Reason.Type << " " << key);
        }
        objs.erase(it);
        return;
    }

    auto& objs = Reason.Type == SelectionChanges::SetPreselect ? objectsOnTopPreSel : objectsOnTop;
    auto pcGroup = Reason.Type == SelectionChanges::SetPreselect ? pcGroupOnTopPreSel
                                                                 : pcGroupOnTopSel;

    if (objs.find(key.c_str()) != objs.end()) {
        return;
    }
    auto vp = freecad_cast<ViewProviderDocumentObject*>(Application::Instance->getViewProvider(obj));
    if (!vp || !vp->isSelectable() || !vp->isShow()) {
        return;
    }
    auto svp = vp;
    if (subname && *subname) {
        auto sobj = obj->getSubObject(subname);
        if (!sobj || !sobj->isAttachedToDocument()) {
            return;
        }
        if (sobj != obj) {
            svp = freecad_cast<ViewProviderDocumentObject*>(
                Application::Instance->getViewProvider(sobj)
            );
            if (!svp || !svp->isSelectable()) {
                return;
            }
        }
    }
    int onTop;
    // onTop==2 means on top only if whole object is selected,
    // onTop==3 means on top only if some sub-element is selected
    // onTop==1 means either
    if (vp->OnTopWhenSelected.getValue()) {
        onTop = vp->OnTopWhenSelected.getValue();
    }
    else {
        onTop = svp->OnTopWhenSelected.getValue();
    }
    if (Reason.Type == SelectionChanges::SetPreselect) {
        SoHighlightElementAction action;
        action.setHighlighted(true);
        action.setColor(selectionRoot->colorHighlight.getValue());
        action.apply(pcGroupOnTopPreSel);
        if (!onTop) {
            onTop = 2;
        }
    }
    else {
        if (!onTop) {
            return;
        }
        SoSelectionElementAction action(SoSelectionElementAction::All);
        action.setColor(selectionRoot->colorSelection.getValue());
        action.apply(pcGroupOnTopSel);
    }
    if (onTop == 2 || onTop == 3) {
        if (subname && *subname) {
            size_t len = strlen(subname);
            if (subname[len - 1] == '.') {
                // ending with '.' means whole object selection
                if (onTop == 3) {
                    return;
                }
            }
            else if (onTop == 2) {
                return;
            }
        }
        else if (onTop == 3) {
            return;
        }
    }

    SoTempPath path(10);
    path.ref();

    if (!appendGroupPath(vp, path)) {
        path.unrefNoDelete();
        return;
    }

    SoDetail* det = nullptr;
    if (vp->getDetailPath(subname, &path, true, det) && path.getLength()) {
        auto node = new SoFCPathAnnotation;
        node->setPath(&path);
        pcGroup->addChild(node);
        if (det) {
            SoSelectionElementAction action(SoSelectionElementAction::Append, true);
            action.setElement(det);
            SoTempPath tmpPath(path.getLength() + 2);
            tmpPath.ref();
            tmpPath.append(pcGroup);
            tmpPath.append(node);
            tmpPath.append(&path);
            action.apply(&tmpPath);
            tmpPath.unrefNoDelete();
            node->setDetail(det);
            det = nullptr;
        }
        FC_LOG("add annotation " << Reason.Type << " " << key);
        objs[key.c_str()] = node;
    }
    delete det;
    path.unrefNoDelete();
}

bool View3DInventorSelection::appendGroupPath(ViewProviderDocumentObject* vp, SoTempPath& path) const
{
    std::vector<ViewProvider*> groups;
    auto grpVp = vp;
    std::set<ViewProvider*> visited;
    for (auto childVp = vp;; childVp = grpVp) {
        auto grp = App::GeoFeatureGroupExtension::getGroupOfObject(childVp->getObject());
        if (!grp || !grp->isAttachedToDocument()) {
            break;
        }

        grpVp = freecad_cast<ViewProviderDocumentObject*>(Application::Instance->getViewProvider(grp));
        if (!grpVp) {
            break;
        }

        // avoid endless-loops
        if (!visited.insert(childVp).second) {
            break;
        }

        auto childRoot = grpVp->getChildRoot();
        auto modeSwitch = grpVp->getModeSwitch();
        auto idx = modeSwitch->whichChild.getValue();
        if (idx < 0 || idx >= modeSwitch->getNumChildren() || modeSwitch->getChild(idx) != childRoot) {
            FC_LOG("skip " << vp->getObject()->getFullName() << ", hidden inside geo group");
            return false;
        }
        if (childRoot->findChild(childVp->getRoot()) < 0) {
            FC_LOG(
                "cannot find '" << childVp->getObject()->getFullName() << "' in geo group '"
                                << grp->getNameInDocument() << "'"
            );
            break;
        }
        groups.push_back(grpVp);
    }

    for (auto it = groups.rbegin(); it != groups.rend(); ++it) {
        auto grpVp = *it;
        path.append(grpVp->getRoot());
        path.append(grpVp->getModeSwitch());
        path.append(grpVp->getChildRoot());
    }
    return true;
}

View3DInventorSelection::HighlightRoleNodes* View3DInventorSelection::highlightRole(HighlightRole role)
{
    const std::size_t index = highlightRoleIndex(role);
    return index < highlightRoles.size() ? &highlightRoles.at(index) : nullptr;
}

void View3DInventorSelection::setHighlightStyle(HighlightRole role, const Base::Color& color, float lineWidth)
{
    HighlightRoleNodes* nodes = highlightRole(role);
    if (!nodes) {
        return;
    }
    nodes->style->lineWidth = lineWidth;
    nodes->color = color.asValue<SbColor>();
}

SoGroup* View3DInventorSelection::highlightOwnerGroup(HighlightRoleNodes& nodes, const void* owner)
{
    auto found = nodes.owners.find(owner);
    if (found != nodes.owners.end()) {
        return found->second;
    }
    // A selection root rather than a plain separator: the secondary selection context
    // carrying the highlight colour is keyed by the whole chain of SoFCSelectionRoot
    // nodes on the path, so without one of its own two owners annotating the same
    // subelement would share a context and either clearing it would strip the other.
    auto selRoot = new SoFCSelectionRoot;
    selRoot->selectionStyle = SoFCSelectionRoot::PassThrough;
    selRoot->setName("HighlightOwner");
    nodes.group->addChild(selRoot);
    nodes.owners.emplace(owner, selRoot);
    return selRoot;
}

void View3DInventorSelection::addHighlight(
    HighlightRole role,
    const void* owner,
    App::DocumentObject* object,
    const char* subName
)
{
    HighlightRoleNodes* nodes = highlightRole(role);
    if (!nodes || !object || !object->isAttachedToDocument() || !Application::Instance) {
        return;
    }
    auto vp = freecad_cast<ViewProviderDocumentObject*>(Application::Instance->getViewProvider(object));
    // A hidden object contributes nothing: the stored path runs through its mode
    // switch, and Coin's in-path traversal honours whichChild.
    if (!vp || !vp->isSelectable() || !vp->isShow()) {
        return;
    }
    // Public API, so the document is checked rather than assumed. An annotation holds
    // a path into its own document's scene graph; hung under another document's viewer
    // it would render that document's geometry here and expose its nodes to this
    // viewer's traversals.
    if (!guiDocument || vp->getDocument() != guiDocument) {
        return;
    }

    SoTempPath path(10);
    path.ref();
    if (!appendGroupPath(vp, path)) {
        path.unrefNoDelete();
        return;
    }

    SoDetail* det = nullptr;
    // The annotation holds a plain SoPath into the view provider's live nodes. A
    // recompute that rebuilds that scene graph makes Coin's path auditing truncate the
    // path rather than leave it dangling, so the highlight silently stops rendering
    // until the next refresh. checkGroupOnTop() has exactly the same exposure.
    if (vp->getDetailPath(subName, &path, true, det) && path.getLength()) {
        auto grp = highlightOwnerGroup(*nodes, owner);
        auto node = new SoFCPathAnnotation;
        node->setPath(&path);
        grp->addChild(node);

        // The colour rides in the secondary selection context rather than an
        // overriding material, because that is what narrows the render to the
        // detail; an override material would repaint the whole object.
        SoSelectionElementAction action(
            det ? SoSelectionElementAction::Append : SoSelectionElementAction::All,
            true
        );
        action.setColor(nodes->color);
        action.setElement(det);

        SoTempPath tmpPath(path.getLength() + 3);
        tmpPath.ref();
        tmpPath.append(nodes->group);
        tmpPath.append(grp);
        tmpPath.append(node);
        tmpPath.append(&path);
        action.apply(&tmpPath);
        tmpPath.unrefNoDelete();

        node->setDetail(det);
        det = nullptr;
    }
    delete det;
    path.unrefNoDelete();
}

void View3DInventorSelection::clearHighlight(HighlightRole role, const void* owner)
{
    HighlightRoleNodes* nodes = highlightRole(role);
    if (!nodes) {
        return;
    }
    auto found = nodes->owners.find(owner);
    if (found == nodes->owners.end()) {
        return;
    }
    SoGroup* grp = found->second;
    nodes->owners.erase(found);

    // Drop the secondary contexts this owner's annotations created before the nodes go
    // away. Applied along the role group so the context key matches the one
    // addHighlight() wrote under; every other owner's subtree is left untouched.
    SoTempPath path(2);
    path.ref();
    path.append(nodes->group);
    path.append(grp);
    SoSelectionElementAction action(SoSelectionElementAction::None, true);
    action.apply(&path);
    path.unrefNoDelete();

    nodes->group->removeChild(grp);
}

void View3DInventorSelection::clearGroupOnTop()
{
    if (!objectsOnTop.empty() || !objectsOnTopPreSel.empty()) {
        objectsOnTop.clear();
        objectsOnTopPreSel.clear();
        SoSelectionElementAction action(SoSelectionElementAction::None, true);
        action.apply(pcGroupOnTopPreSel);
        action.apply(pcGroupOnTopSel);
        coinRemoveAllChildren(pcGroupOnTopSel);
        coinRemoveAllChildren(pcGroupOnTopPreSel);
        FC_LOG("clear annotation");
    }
}
