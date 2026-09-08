#pragma once

#include "../plugin.hpp"
#include "../core/Video.hpp"
#include "AdapterDiagnostics.hpp"
#include "PublisherNames.hpp"

#include <condition_variable>
#include <map>
#include <thread>

namespace rvx {
namespace rackadapter {

class PatchService;

// The process-wide object indexes context services and process-unique UI names,
// and retires workers without blocking Rack's engine lock. Each Rack Engine*
// receives an isolated PatchService and video Engine.
class ServiceRegistry {
public:
    static ServiceRegistry& instance();
    std::shared_ptr<PatchService> attach(rack::engine::Engine* context,
                                         const std::shared_ptr<Node>& node,
                                         std::string* automaticPublisherName = NULL);
    void detach(rack::engine::Engine* context, uint64_t nodeKey);
    void observePublisherName(uint64_t nodeKey, const std::string& name);
    void updatePublisherName(const std::shared_ptr<Node>& node,
                             std::string* automaticPublisherName);
    bool publisherNameConflict(uint64_t nodeKey);
    size_t invalidNativeOutputCount(uint64_t nodeKey);
    ~ServiceRegistry();

private:
    ServiceRegistry();
    ServiceRegistry(const ServiceRegistry&);
    ServiceRegistry& operator=(const ServiceRegistry&);
    void reap();

    std::mutex mutex_;
    std::map<rack::engine::Engine*, std::shared_ptr<PatchService> > services_;
    std::map<uint64_t, rack::engine::Engine*> nodeContexts_;
    PublisherNameReservations publisherNames_;
    std::vector<std::shared_ptr<PatchService> > retired_;
    std::condition_variable condition_;
    bool closing_ = false;
    std::thread reaper_;
};

class PatchService {
public:
    PatchService();
    ~PatchService();
    void start();
    void stop();
    void addNode(const std::shared_ptr<Node>& node);
    bool removeNode(uint64_t key);
    void hintTopologyDirty() noexcept;
    void syncRackUi();
    EngineStats stats() const;
    size_t invalidNativeOutputCount(uint64_t nodeKey) const;

private:
    bool sameGraph(const Graph& graph) const;

    mutable std::mutex mutex_;
    std::map<uint64_t, std::shared_ptr<Node> > nodes_;
    NativeCableDiagnostics cableDiagnostics_;
    Graph submitted_;
    Engine engine_;
    std::atomic<bool> topologyDirty_{true};
    std::atomic<bool> stopped_{false};
    double lastUiFrameTime_ = -1.0;
    double lastDiagnosticsTime_ = -1.0;
    uint64_t nextRevision_ = 1;
    size_t adapterErrors_ = 0;
    bool diagnostics_ = false;
};

class Module : public rack::engine::Module {
public:
    explicit Module(Kind kind);
    ~Module() override;
    Kind kind() const { return kind_; }
    std::shared_ptr<Node> node() const { return node_; }
    std::shared_ptr<PatchService> service() const { return service_; }

    void process(const ProcessArgs& args) override;
    void processBypass(const ProcessArgs& args) override;
    void onAdd(const AddEvent& e) override;
    void onRemove(const RemoveEvent& e) override;
    void onPortChange(const PortChangeEvent& e) override;
    void onSampleRateChange(const SampleRateChangeEvent& e) override;
    void onBypass(const BypassEvent& e) override;
    void onUnBypass(const UnBypassEvent& e) override;
    void onReset(const ResetEvent& e) override;
    void fromJson(json_t* rootJ) override;
    json_t* dataToJson() override;
    void dataFromJson(json_t* rootJ) override;

protected:
    void attachNode(const AddEvent& e, std::string* automaticPublisherName = NULL);
    virtual void capture(const ProcessArgs& args);
    virtual void appendData(json_t* rootJ) const;
    virtual void readData(json_t* rootJ, int schema);
    virtual void prepareRestoredState();
    void zeroNativeVideoOutputs() noexcept;
    void incrementTrigger(std::atomic<uint64_t>& counter) noexcept;
    void publishRestoredState();

    Kind kind_;
    std::shared_ptr<Node> node_;
    std::shared_ptr<PatchService> service_;
    rack::engine::Engine* context_ = NULL;
    bool registered_ = false;
    bool restoringModuleJson_ = false;
    double audioSeconds_ = 0.0;
    uint64_t audioEpoch_ = 0;
    int64_t lastRackFrame_ = -1;
};

// This subclass tags only suite-owned video-domain ports. Its pointer is read
// synchronously on Rack's UI thread and is never copied into a Graph.
class VideoPort : public rack::app::PortWidget {
public:
    VideoPort();
    void bind(const std::shared_ptr<Node>& node, PortType type, int portId);
    uint64_t nodeKey() const { return nodeKey_; }
    PortType videoType() const { return videoType_; }
    int videoPortId() const { return videoPortId_; }
    void draw(const DrawArgs& args) override;

private:
    uint64_t nodeKey_ = 0;
    PortType videoType_ = PortType::None;
    int videoPortId_ = -1;
};

class ModuleWidget : public rack::app::ModuleWidget {
public:
    explicit ModuleWidget(Module* module);
    void step() override;

protected:
    VideoPort* addVideoInput(rack::math::Vec position, int portId, PortType type);
    VideoPort* addVideoOutput(rack::math::Vec position, int portId, PortType type);
    Module* rvxModule_ = NULL;
};

} // namespace rackadapter
} // namespace rvx
