//
// Copyright (c) 2008-2020 the Urho3D project.
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

#pragma once

#include "Sample.h"

#include <Urho3D/Network/Protocol.h>
#include <Urho3D/RmlUI/RmlUIComponent.h>

#include <RmlUi/Core/DataModelHandle.h>

namespace Urho3D
{

class Button;
class Connection;
class Scene;
class Text;
class UIElement;

}

/// UI widget to manage server and client settings.
class AdvancedNetworkingUI : public RmlUIComponent
{
    URHO3D_OBJECT(AdvancedNetworkingUI, RmlUIComponent);

public:
    using RmlUIComponent::RmlUIComponent;

    void StartServer();
    void ConnectToServer(const ea::string& address);
    void Stop();

private:
    void OnNodeSet(Node* node) override;
    void Update(float timeStep) override;

    Rml::DataModelHandle model_;
    int serverPort_{2345};
    Rml::String connectionAddress_{"localhost"};
};

/// Scene network replication example.
/// This sample demonstrates:
///     - Creating a scene in which network clients can join
///     - Giving each client an object to control and sending the controls from the clients to the server
///       where the authoritative simulation happens
///     - Controlling a physics object's movement by applying forces
class AdvancedNetworking : public Sample
{
    URHO3D_OBJECT(AdvancedNetworking, Sample);

public:
    /// Construct.
    explicit AdvancedNetworking(Context* context);

    /// Setup after engine initialization and before running the main loop.
    void Start(const ea::vector<ea::string>& args) override;

protected:
    /// Return XML patch instructions for screen joystick layout for a specific sample app, if any.
    ea::string GetScreenJoystickPatchString() const override { return
        "<patch>"
        "    <add sel=\"/element/element[./attribute[@name='Name' and @value='Hat0']]\">"
        "        <attribute name=\"Is Visible\" value=\"false\" />"
        "    </add>"
        "</patch>";
    }

private:
    /// Construct the scene content.
    void CreateScene();
    /// Construct instruction text and the login / start server UI.
    void CreateUI();
    /// Set up viewport.
    void SetupViewport();
    /// Subscribe to update, UI and network events.
    void SubscribeToEvents();
    /// Create a controllable ball object and return its scene node.
    Node* CreateControllableObject(Connection* owner);
    /// Read input and move the camera.
    void MoveCamera();
    /// Update statistics text.
    void UpdateStats();
    /// Handle the logic post-update event.
    void HandlePostUpdate(StringHash eventType, VariantMap& eventData);
    /// Handle a client connecting to the server.
    void HandleClientConnected(StringHash eventType, VariantMap& eventData);
    /// Handle a client disconnecting from the server.
    void HandleClientDisconnected(StringHash eventType, VariantMap& eventData);

    /// UI with client and server settings.
    AdvancedNetworkingUI* ui_{};

    /// Mapping from client connections to controllable objects.
    ea::unordered_map<Connection*, WeakPtr<Node> > serverObjects_;
    /// Instructions text.
    SharedPtr<Text> instructionsText_;

    /// Text with statistics
    SharedPtr<Text> statsText_;
    /// Statistics UI update timer
    Timer statsTimer_;
};
