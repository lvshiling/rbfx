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

#include "../Project/CreateDefaultScene.h"

#include <Urho3D/Graphics/Light.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Graphics/Model.h>
#include <Urho3D/Graphics/Octree.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/Graphics/Zone.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Resource/XMLFile.h>
#include <Urho3D/Scene/Scene.h>

namespace Urho3D
{

void CreateDefaultScene(Context* context, const ea::string& fileName, const DefaultSceneParameters& params)
{
    auto cache = context->GetSubsystem<ResourceCache>();

    auto scene = MakeShared<Scene>(context);
    scene->CreateComponent<Octree>();

    if (params.createObjects_)
    {
        auto zoneNode = scene->CreateChild("Global Zone");
        auto zone = zoneNode->CreateComponent<Zone>();
        zone->SetBoundingBox(BoundingBox{-1000.0f, 1000.0f});
        zone->SetFogColor(0x0072A0_rgb);
        zone->SetAmbientColor(Color::BLACK);
        zone->SetBackgroundBrightness(0.5f);

        auto lightNode = scene->CreateChild("Global Light");
        lightNode->SetDirection({1.0f, -1.0f, 1.0f});
        auto light = lightNode->CreateComponent<Light>();
        light->SetLightType(LIGHT_DIRECTIONAL);
        light->SetColor(Color::WHITE);
        light->SetBrightness(0.5f);

        auto cubeNode = scene->CreateChild("Sample Cube");
        cubeNode->SetScale(3.0f);
        auto cubeGeometry = cubeNode->CreateComponent<StaticModel>();
        cubeGeometry->SetModel(cache->GetResource<Model>("Models/TeaPot.mdl"));
        cubeGeometry->SetMaterial(cache->GetResource<Material>("Materials/DefaultWhite.xml"));
    }

    auto xmlFile = MakeShared<XMLFile>(context);
    XMLElement xmlRoot = xmlFile->CreateRoot("scene");
    scene->SaveXML(xmlRoot);
    xmlFile->SaveFile(fileName);
}

}
