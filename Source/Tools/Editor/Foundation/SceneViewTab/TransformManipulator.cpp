//
// Copyright (c) 2017-2020 the rbfx project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "../../Core/CommonEditorActions.h"
#include "../../Foundation/SceneViewTab/TransformManipulator.h"

#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Graphics/Octree.h>
#include <Urho3D/SystemUI/SystemUI.h>

#include <ImGuizmo/ImGuizmo.h>

namespace Urho3D
{

namespace
{

URHO3D_EDITOR_SCOPE(Scope_TransformManipulator, "TransformManipulator");
URHO3D_EDITOR_SCOPED_HOTKEY(Hotkey_ToggleSpace,
    "TransformManipulator.ToggleSpace", Scope_TransformManipulator, QUAL_NONE, KEY_X);

}

void Foundation_TransformManipulator(Context* context, SceneViewTab* sceneViewTab)
{
    auto project = sceneViewTab->GetProject();
    auto settingsManager = project->GetSettingsManager();

    auto settingsPage = MakeShared<TransformManipulator::SettingsPage>(context);
    settingsManager->AddPage(settingsPage);

    sceneViewTab->RegisterAddon<TransformManipulator>(settingsPage);
}

void TransformManipulator::Settings::SerializeInBlock(Archive& archive)
{
    SerializeOptionalValue(archive, "SnapPosition", snapPosition_);
    SerializeOptionalValue(archive, "SnapRotation", snapRotation_);
    SerializeOptionalValue(archive, "SnapScale", snapScale_);
}

void TransformManipulator::Settings::RenderSettings()
{
    ui::DragFloat("Snap Position", &snapPosition_, 0.001f, 0.001f, 10.0f, "%.3f");
    ui::DragFloat("Snap Rotation", &snapRotation_, 0.001f, 0.001f, 360.0f, "%.3f");
    ui::DragFloat("Snap Scale", &snapScale_, 0.001f, 0.001f, 1.0f, "%.3f");
}

TransformManipulator::TransformManipulator(SceneViewTab* owner, SettingsPage* settings)
    : SceneViewAddon(owner)
    , settings_(settings)
{
    auto project = owner_->GetProject();
    HotkeyManager* hotkeyManager = project->GetHotkeyManager();
    hotkeyManager->BindHotkey(this, Hotkey_ToggleSpace, &TransformManipulator::ToggleSpace);
}

void TransformManipulator::ProcessInput(SceneViewPage& scenePage, bool& mouseConsumed)
{
    if (!settings_)
        return;
    const Settings& cfg = settings_->GetValues();

    const auto& nodes = scenePage.selection_.GetEffectiveNodes();
    if (nodes.empty())
        return;

    EnsureGizmoInitialized(scenePage.selection_);

    if (!mouseConsumed)
    {
        Camera* camera = scenePage.renderer_->GetCamera();
        const TransformGizmo gizmo{camera, scenePage.contentArea_};

        const bool needSnap = ui::IsKeyDown(KEY_CTRL);
        if (transformGizmo_->Manipulate(gizmo, TransformGizmoOperation::Translate, isLocal_, needSnap ? cfg.snapPosition_ : 0.0f))
            mouseConsumed = true;
    }
}

void TransformManipulator::EnsureGizmoInitialized(SceneSelection& selection)
{
    if (selection.GetRevision() != selectionRevision_)
    {
        selectionRevision_ = selection.GetRevision();
        transformGizmo_ = ea::nullopt;
    }

    if (!transformGizmo_)
    {
        const auto& nodes = selection.GetEffectiveNodes();
        const Node* anchorNode = selection.GetAnchor();
        transformGizmo_ = TransformNodesGizmo{anchorNode, nodes.begin(), nodes.end()};
        transformGizmo_->OnNodeTransformChanged.Subscribe(this, &TransformManipulator::OnNodeTransformChanged);
    }
}

void TransformManipulator::OnNodeTransformChanged(Node* node, const Transform& oldTransform)
{
    owner_->PushWrappedAction<ChangeNodeTransformAction>(node, oldTransform);
}

void TransformManipulator::UpdateAndRender(SceneViewPage& scenePage)
{
}

void TransformManipulator::ApplyHotkeys(HotkeyManager* hotkeyManager)
{
    hotkeyManager->InvokeScopedHotkeys(Scope_TransformManipulator);
}

}
