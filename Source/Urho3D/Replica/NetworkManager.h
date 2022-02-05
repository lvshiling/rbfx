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

/// \file

#pragma once

#include "../Container/TransformedSpan.h"
#include "../Core/Signal.h"
#include "../IO/MemoryBuffer.h"
#include "../IO/VectorBuffer.h"
#include "../Replica/ClientReplica.h"
#include "../Replica/ProtocolMessages.h"
#include "../Replica/ServerReplicator.h"
#include "../Scene/TrackedComponent.h"

#include <EASTL/optional.h>
#include <EASTL/unordered_set.h>

namespace Urho3D
{

class Connection;
class Network;
class NetworkObject;
class NetworkSetting;

/// Part of NetworkManager used by both client and server, and referenced by components.
class URHO3D_API NetworkManagerBase : public BaseStableComponentRegistry
{
    URHO3D_OBJECT(NetworkManagerBase, BaseStableComponentRegistry);

public:
    Signal<void(NetworkObject*)> OnNetworkObjectAdded;
    Signal<void(NetworkObject*)> OnNetworkObjectRemoved;

    explicit NetworkManagerBase(Context* context);

    /// Process components
    /// @{
    void QueueComponentUpdate(NetworkObject* networkObject);
    void RemoveAllComponents();

    void UpdateAndSortNetworkObjects(ea::vector<NetworkObject*>& networkObjects) const;
    /// @}

    Scene* GetScene() const { return scene_; }
    const auto GetNetworkObjects() const { return StaticCastSpan<NetworkObject* const>(GetTrackedComponents()); }
    unsigned GetNetworkIndexUpperBound() const { return GetStableIndexUpperBound(); }
    NetworkObject* GetNetworkObject(NetworkId networkId, bool checkVersion = true) const;
    NetworkObject* GetNetworkObjectByIndex(unsigned networkIndex) const;

private:
    Scene* scene_{};

    ea::vector<bool> networkObjectsDirty_;

protected:
    void OnSceneSet(Scene* scene) override;
    void OnComponentAdded(BaseTrackedComponent* baseComponent) override;
    void OnComponentRemoved(BaseTrackedComponent* baseComponent) override;
};

enum ReplicationManagerMode
{
    Standalone,
    Server,
    Client
};

/// Root level scene component that manages Scene replication both on client and server.
/// TODO(network): Rename to ReplicationManager
class URHO3D_API NetworkManager : public NetworkManagerBase
{
    URHO3D_OBJECT(NetworkManager, NetworkManagerBase);

public:
    explicit NetworkManager(Context* context);
    ~NetworkManager() override;

    static void RegisterObject(Context* context);

    /// Stop whatever client or server logic is going on and continue standalone.
    void StartStandalone();
    /// Start new server from current state.
    void StartServer();
    /// Start new client from specified connection. Removes all existing objects.
    void StartClient(AbstractConnection* connectionToServer);
    /// Process network message either as client or as server.
    void ProcessMessage(AbstractConnection* connection, NetworkMessageId messageId, MemoryBuffer& messageData);
    /// Process connection dropped. Removes client connection for server, converts scene to standalone for client.
    void DropConnection(AbstractConnection* connection);

    /// Return current state specific to client or server.
    /// @{
    unsigned GetUpdateFrequency() const;
    float GetTraceDurationInSeconds() const;
    unsigned GetTraceDurationInFrames() const;
    const Variant& GetSetting(const NetworkSetting& setting) const;
    ea::string GetDebugInfo() const;
    bool IsStandalone() const { return mode_ == ReplicationManagerMode::Standalone; }
    bool IsServer() const { return mode_ == ReplicationManagerMode::Server; }
    bool IsClient() const { return mode_ == ReplicationManagerMode::Client; }
    ClientReplica* GetClientReplica() const { return client_ ? client_->replica_ : nullptr; }
    ServerReplicator* GetServerReplicator() const { return server_; }
    /// @}

protected:
    void OnComponentAdded(BaseTrackedComponent* baseComponent) override;

private:
    void Stop();
    void ProcessMessageOnUninitializedClient(
        AbstractConnection* connection, NetworkMessageId messageId, MemoryBuffer& messageData);

    struct ClientData
    {
        WeakPtr<AbstractConnection> connection_;
        ea::optional<MsgSceneClock> initialClock_;
        ea::optional<VariantMap> serverSettings_;
        ea::optional<unsigned> ackMagic_;

        bool IsReadyToInitialize() const { return connection_ && initialClock_ && serverSettings_ && ackMagic_; }

        SharedPtr<ClientReplica> replica_;
    };

    ReplicationManagerMode mode_{};
    SharedPtr<ServerReplicator> server_;
    ea::optional<ClientData> client_;
};

}
