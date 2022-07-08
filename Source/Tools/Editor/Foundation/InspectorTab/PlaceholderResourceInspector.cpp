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

#include "../../Foundation/InspectorTab/PlaceholderResourceInspector.h"

#include <IconFontCppHeaders/IconsFontAwesome6.h>

namespace Urho3D
{

void Foundation_PlaceholderResourceInspector(Context* context, InspectorTab_* inspectorTab)
{
    inspectorTab->RegisterAddon<PlaceholderResourceInspector>();
}

PlaceholderResourceInspector::PlaceholderResourceInspector(InspectorTab_* owner)
    : InspectorAddon(owner)
{
    auto project = owner_->GetProject();
    project->OnRequest.Subscribe(this, &PlaceholderResourceInspector::OnProjectRequest);
}

void PlaceholderResourceInspector::OnProjectRequest(ProjectRequest* request)
{
    auto inspectResourceRequest = dynamic_cast<InspectResourceRequest*>(request);
    if (!inspectResourceRequest || inspectResourceRequest->GetResources().empty())
        return;

    request->QueueProcessCallback([=]()
    {
        InspectResources(inspectResourceRequest->GetResources());
    }, M_MIN_INT);
}

void PlaceholderResourceInspector::InspectResources(const ea::vector<FileResourceDesc>& resources)
{
    if (resources.size() == 1)
    {
        const FileResourceDesc& desc = resources.front();
        const ea::string resourceType = desc.IsValidFile() ? "File" : "Folder";
        singleResource_ = SingleResource{resourceType, desc.GetResourceName()};
        multipleResources_ = ea::nullopt;
    }
    else
    {
        const unsigned numFolders = std::count_if(resources.begin(), resources.end(),
            [](const FileResourceDesc& desc) { return !desc.IsValidFile(); });
        const unsigned numFiles = resources.size() - numFolders;
        multipleResources_ = MultipleResources{numFiles, numFolders};
        singleResource_ = ea::nullopt;
    }

    Activate();
}

void PlaceholderResourceInspector::RenderContent()
{
    if (singleResource_)
    {
        if (ui::Button(Format("Open {}", singleResource_->resourceType_).c_str()))
        {
            auto project = owner_->GetProject();
            auto request = MakeShared<OpenResourceRequest>(context_, singleResource_->resourceName_);
            project->ProcessRequest(request);
        }

        ui::TextWrapped("%s", singleResource_->resourceName_.c_str());
    }
    else if (multipleResources_)
    {
        ui::Text("%u files selected", multipleResources_->numFiles_);
        ui::Text("%u folders selected", multipleResources_->numFolders_);
    }
}

void PlaceholderResourceInspector::RenderContextMenuItems()
{
}

void PlaceholderResourceInspector::RenderMenu()
{
}

void PlaceholderResourceInspector::ApplyHotkeys(HotkeyManager* hotkeyManager)
{
}

}