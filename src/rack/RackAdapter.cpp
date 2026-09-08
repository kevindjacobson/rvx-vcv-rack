#include "RackAdapter.hpp"
#include "../io/VideoBackend.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <tuple>

namespace rvx {
namespace rackadapter {

namespace {

bool connectionLess(const Connection& a, const Connection& b) {
    return std::make_tuple(a.destination, a.input, a.source, a.output)
        < std::make_tuple(b.destination, b.input, b.source, b.output);
}

bool sameConnection(const Connection& a, const Connection& b) {
    return a.source == b.source && a.output == b.output
        && a.destination == b.destination && a.input == b.input;
}

} // namespace

ServiceRegistry& ServiceRegistry::instance() {
    static ServiceRegistry registry;
    return registry;
}

ServiceRegistry::ServiceRegistry()
    : reaper_(&ServiceRegistry::reap, this) {
}

ServiceRegistry::~ServiceRegistry() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (std::map<::rack::engine::Engine*, std::shared_ptr<PatchService> >::iterator it = services_.begin();
             it != services_.end(); ++it) {
            retired_.push_back(it->second);
        }
        services_.clear();
        closing_ = true;
    }
    condition_.notify_one();
    if (reaper_.joinable())
        reaper_.join();
}

std::shared_ptr<PatchService> ServiceRegistry::attach(
    ::rack::engine::Engine* context, const std::shared_ptr<Node>& node,
    std::string* automaticPublisherName) {
    if (!node)
        return std::shared_ptr<PatchService>();

    std::lock_guard<std::mutex> lock(mutex_);
    if (node->kind == Kind::VideoIo) {
        IoSettings io = node->ioSettings();
        const bool automatic = automaticPublisherName != NULL;
        const std::string preferred = automatic ? *automaticPublisherName : io.publisherName;
        const std::string selected = publisherNames_.reserve(node->key, preferred, automatic);
        if (automatic) {
            io.publisherName = selected;
            node->setIoSettings(io);
            *automaticPublisherName = selected;
        }
    }
    if (!context)
        return std::shared_ptr<PatchService>();

    std::shared_ptr<PatchService>& service = services_[context];
    bool created = false;
    if (!service) {
        service = std::shared_ptr<PatchService>(new PatchService);
        created = true;
    }
    service->addNode(node);
    nodeContexts_[node->key] = context;
    if (created)
        service->start();
    return service;
}

void ServiceRegistry::detach(::rack::engine::Engine* context, uint64_t nodeKey) {
    std::lock_guard<std::mutex> lock(mutex_);
    publisherNames_.release(nodeKey);
    nodeContexts_.erase(nodeKey);
    if (!context)
        return;
    std::map<::rack::engine::Engine*, std::shared_ptr<PatchService> >::iterator it = services_.find(context);
    if (it == services_.end())
        return;
    std::shared_ptr<PatchService> service = it->second;
    if (service->removeNode(nodeKey)) {
        services_.erase(it);
        retired_.push_back(service);
        condition_.notify_one();
    }
}

void ServiceRegistry::observePublisherName(uint64_t nodeKey, const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    publisherNames_.observe(nodeKey, name);
}

void ServiceRegistry::updatePublisherName(const std::shared_ptr<Node>& node,
                                          std::string* automaticPublisherName) {
    if (!node || node->kind != Kind::VideoIo)
        return;
    std::lock_guard<std::mutex> lock(mutex_);
    IoSettings io = node->ioSettings();
    const bool automatic = automaticPublisherName != NULL;
    const std::string preferred = automatic ? *automaticPublisherName : io.publisherName;
    const std::string selected = publisherNames_.reserve(node->key, preferred, automatic);
    if (automatic) {
        io.publisherName = selected;
        node->setIoSettings(io);
        *automaticPublisherName = selected;
    }
}

bool ServiceRegistry::publisherNameConflict(uint64_t nodeKey) {
    std::lock_guard<std::mutex> lock(mutex_);
    return publisherNames_.hasCollision(nodeKey);
}

size_t ServiceRegistry::invalidNativeOutputCount(uint64_t nodeKey) {
    std::shared_ptr<PatchService> service;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::map<uint64_t, ::rack::engine::Engine*>::const_iterator context = nodeContexts_.find(nodeKey);
        if (context == nodeContexts_.end())
            return 0;
        std::map<::rack::engine::Engine*, std::shared_ptr<PatchService> >::const_iterator found =
            services_.find(context->second);
        if (found == services_.end())
            return 0;
        service = found->second;
    }
    return service->invalidNativeOutputCount(nodeKey);
}

void ServiceRegistry::reap() {
    for (;;) {
        std::shared_ptr<PatchService> service;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this]() { return closing_ || !retired_.empty(); });
            if (retired_.empty()) {
                if (closing_)
                    return;
                continue;
            }
            service = retired_.back();
            retired_.pop_back();
        }
        // stop() can join the video worker, so it must run outside Rack's
        // Engine lock and outside this registry's mutex.
        service->stop();
    }
}

PatchService::PatchService()
    : engine_(Format(), []() { return makeSyphonBackend(); }) {
    const char* value = std::getenv("RVX_DIAGNOSTICS");
    diagnostics_ = value && std::string(value) == "1";
}

PatchService::~PatchService() {
    stop();
}

void PatchService::start() {
    stopped_.store(false, std::memory_order_release);
    engine_.start();
}

void PatchService::stop() {
    if (!stopped_.exchange(true, std::memory_order_acq_rel))
        engine_.stop();
}

void PatchService::addNode(const std::shared_ptr<Node>& node) {
    std::lock_guard<std::mutex> lock(mutex_);
    nodes_[node->key] = node;
    topologyDirty_.store(true, std::memory_order_release);
}

bool PatchService::removeNode(uint64_t key) {
    std::lock_guard<std::mutex> lock(mutex_);
    nodes_.erase(key);
    topologyDirty_.store(true, std::memory_order_release);
    return nodes_.empty();
}

void PatchService::hintTopologyDirty() noexcept {
    topologyDirty_.store(true, std::memory_order_release);
}

bool PatchService::sameGraph(const Graph& graph) const {
    if (graph.nodes.size() != submitted_.nodes.size()
        || graph.connections.size() != submitted_.connections.size())
        return false;
    for (size_t i = 0; i < graph.nodes.size(); ++i) {
        if (graph.nodes[i]->key != submitted_.nodes[i]->key)
            return false;
    }
    for (size_t i = 0; i < graph.connections.size(); ++i) {
        if (!sameConnection(graph.connections[i], submitted_.connections[i]))
            return false;
    }
    return true;
}

void PatchService::syncRackUi() {
    if (!APP || !APP->window || !APP->scene || !APP->scene->rack)
        return;

    // getFrameTime() is identical for every widget stepped in one UI frame.
    // Any RVX widget can therefore keep the service alive without assigning
    // graph ownership to a monitor or another particular panel.
    const double frameTime = APP->window->getFrameTime();
    if (frameTime == lastUiFrameTime_)
        return;
    lastUiFrameTime_ = frameTime;

    Graph graph;
    std::vector<std::pair<uint64_t, std::string> > publisherNames;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (std::map<uint64_t, std::shared_ptr<Node> >::const_iterator it = nodes_.begin();
             it != nodes_.end(); ++it) {
            if (it->second->kind == Kind::VideoIo)
                publisherNames.push_back(std::make_pair(it->first, it->second->ioSettings().publisherName));
            graph.nodes.push_back(it->second);
        }
    }
    for (size_t i = 0; i < publisherNames.size(); ++i)
        ServiceRegistry::instance().observePublisherName(publisherNames[i].first, publisherNames[i].second);

    const std::vector<::rack::app::CableWidget*> cables = APP->scene->rack->getCompleteCables();
    graph.connections.reserve(cables.size());
    size_t adapterErrors = 0;
    std::map<uint64_t, size_t> invalidNativeOutputs;
    for (size_t i = 0; i < cables.size(); ++i) {
        ::rack::app::CableWidget* cable = cables[i];
        if (!cable || !cable->inputPort || !cable->outputPort)
            continue;

        VideoPort* input = dynamic_cast<VideoPort*>(cable->inputPort);
        VideoPort* output = dynamic_cast<VideoPort*>(cable->outputPort);
        if (!input && !output)
            continue;

        Connection connection;
        if (output) {
            connection.source = output->nodeKey();
            connection.output = output->videoPortId();
        }
        else {
            // Preserve a stock-output-to-video-input cable as an invalid edge.
            // The renderer can attach an error to the destination and supply
            // its defined empty input rather than silently hiding the cable.
            connection.source = 0;
            connection.output = -1;
            ++adapterErrors;
        }

        if (input) {
            connection.destination = input->nodeKey();
            connection.input = input->videoPortId();
            graph.connections.push_back(connection);
        }
        else if (output) {
            ++adapterErrors;
            ++invalidNativeOutputs[output->nodeKey()];
        }
        // A suite video output patched into a stock Rack input has no video
        // consumer. Module::process() still drives the native voltage to zero.
    }
    std::sort(graph.connections.begin(), graph.connections.end(), connectionLess);

    bool graphChanged = false;
    bool diagnosticsChanged = false;
    uint64_t revision = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        graphChanged = !sameGraph(graph);
        diagnosticsChanged = cableDiagnostics_.replaceInvalidOutputs(invalidNativeOutputs);
        adapterErrors_ = adapterErrors;
        if (graphChanged || diagnosticsChanged)
            revision = nextRevision_++;
        if (graphChanged) {
            graph.revision = revision;
            submitted_ = graph;
        }
        topologyDirty_.store(false, std::memory_order_release);
    }
    if (graphChanged)
        engine_.submit(graph);
    if ((graphChanged || diagnosticsChanged) && diagnostics_) {
        INFO("RVX adapter revision=%llu nodes=%zu edges=%zu adapterErrors=%zu",
            static_cast<unsigned long long>(revision), graph.nodes.size(),
            graph.connections.size(), adapterErrors);
    }

    if (diagnostics_ && (lastDiagnosticsTime_ < 0.0 || frameTime - lastDiagnosticsTime_ >= 1.0)) {
        lastDiagnosticsTime_ = frameTime;
        const EngineStats s = engine_.stats();
        uint64_t monitorSequence = 0;
        uint64_t sourceSequence = 0;
        for (size_t i = 0; i < graph.nodes.size(); ++i) {
            std::shared_ptr<const NodeDisplay> display = graph.nodes[i]->display();
            if (!display)
                continue;
            if (graph.nodes[i]->kind == Kind::Monitor && display->preview)
                monitorSequence = std::max(monitorSequence, display->preview->sequence);
            if (graph.nodes[i]->kind == Kind::VideoIo && display->outputs[0])
                sourceSequence = std::max(sourceSequence, display->outputs[0]->sequence);
        }
        INFO("RVX diagnostics workerRun=%llu renderFrames=%llu renderDeadlineMisses=%llu skippedTicks=%llu lateTickSlots=%llu p50Ms=%.3f p95Ms=%.3f p99Ms=%.3f lastMs=%.3f maxMs=%.3f histogramSaturated=%llu renderErrors=%llu renderErrorFrames=%llu frameBytes=%zu nodes=%zu edges=%zu adapterErrors=%zu monitorSequence=%llu sourceSequence=%llu",
            static_cast<unsigned long long>(s.workerRun),
            static_cast<unsigned long long>(s.renderMilliseconds.count),
            static_cast<unsigned long long>(s.renderDeadlineMisses),
            static_cast<unsigned long long>(s.skippedTicks),
            static_cast<unsigned long long>(s.lateFrames),
            s.renderMilliseconds.quantile(.50), s.renderMilliseconds.quantile(.95),
            s.renderMilliseconds.quantile(.99), s.lastMilliseconds, s.maxMilliseconds,
            static_cast<unsigned long long>(s.renderMilliseconds.saturated),
            static_cast<unsigned long long>(s.renderErrors),
            static_cast<unsigned long long>(s.renderErrorFrames), s.frameBytes,
            graph.nodes.size(), graph.connections.size(), adapterErrors_,
            static_cast<unsigned long long>(monitorSequence),
            static_cast<unsigned long long>(sourceSequence));
    }
}

EngineStats PatchService::stats() const {
    return engine_.stats();
}

size_t PatchService::invalidNativeOutputCount(uint64_t nodeKey) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cableDiagnostics_.invalidOutputCount(nodeKey);
}

Module::Module(Kind kind)
    : kind_(kind), node_(new Node(kind)) {
    IoSettings io;
    io.publisherName = "RVX";
    node_->setIoSettings(io);
}

Module::~Module() {
    if (registered_) {
        ServiceRegistry::instance().detach(context_, node_->key);
        service_.reset();
        context_ = NULL;
        registered_ = false;
    }
}

void Module::process(const ProcessArgs& args) {
    if (kind_ == Kind::CvBridge && lastRackFrame_ >= 0 && args.frame != lastRackFrame_ + 1) {
        ++audioEpoch_;
        incrementTrigger(node_->resets);
    }
    lastRackFrame_ = args.frame;
    for (size_t i = 0; i < params.size() && i < node_->params.size(); ++i)
        node_->params[i].store(params[i].getValue(), std::memory_order_relaxed);
    capture(args);
    zeroNativeVideoOutputs();
    audioSeconds_ += args.sampleTime;
}

void Module::processBypass(const ProcessArgs& args) {
    // Video bypass is interpreted by the RVX renderer. Continue bounded input
    // capture while bypassed so unbypass does not expose a stale bridge state.
    process(args);
}

void Module::capture(const ProcessArgs& args) {
    (void) args;
}

void Module::zeroNativeVideoOutputs() noexcept {
    for (size_t i = 0; i < outputs.size(); ++i) {
        if (outputType(kind_, static_cast<int>(i)) == PortType::Audio)
            continue;
        outputs[i].setChannels(1);
        outputs[i].setVoltage(0.f, 0);
    }
}

void Module::incrementTrigger(std::atomic<uint64_t>& counter) noexcept {
    uint64_t value = counter.load(std::memory_order_relaxed);
    while (value != UINT64_MAX
           && !counter.compare_exchange_weak(value, value + 1,
               std::memory_order_release, std::memory_order_relaxed)) {
    }
}

void Module::onAdd(const AddEvent& e) {
    attachNode(e, NULL);
}

void Module::attachNode(const AddEvent& e, std::string* automaticPublisherName) {
    ::rack::engine::Module::onAdd(e);
    for (size_t i = 0; i < params.size() && i < node_->params.size(); ++i)
        node_->params[i].store(params[i].getValue(), std::memory_order_relaxed);
    node_->bypass.store(isBypassed(), std::memory_order_release);
    context_ = APP ? APP->engine : NULL;
    service_ = ServiceRegistry::instance().attach(context_, node_, automaticPublisherName);
    registered_ = true;
}

void Module::onRemove(const RemoveEvent& e) {
    if (registered_) {
        ServiceRegistry::instance().detach(context_, node_->key);
        service_.reset();
        context_ = NULL;
        registered_ = false;
    }
    ::rack::engine::Module::onRemove(e);
}

void Module::onPortChange(const PortChangeEvent& e) {
    (void) e;
    // Rack calls this while holding its Engine writer lock. Do not query Rack,
    // allocate a graph, join, or enter a lock used by the video worker here.
    if (service_)
        service_->hintTopologyDirty();
}

void Module::onSampleRateChange(const SampleRateChangeEvent& e) {
    ::rack::engine::Module::onSampleRateChange(e);
    // Keep the accumulated timestamp monotonic while marking a new sampling
    // epoch. The renderer discards buffered samples from earlier epochs and
    // establishes a fresh audio-to-video time mapping from the first new one.
    if (kind_ == Kind::CvBridge) {
        ++audioEpoch_;
        incrementTrigger(node_->resets);
    }
    lastRackFrame_ = -1;
}

void Module::onBypass(const BypassEvent& e) {
    (void) e;
    node_->bypass.store(true, std::memory_order_release);
}

void Module::onUnBypass(const UnBypassEvent& e) {
    (void) e;
    node_->bypass.store(false, std::memory_order_release);
}

void Module::onReset(const ResetEvent& e) {
    ::rack::engine::Module::onReset(e);
    incrementTrigger(node_->resets);
    if (kind_ == Kind::CvBridge)
        ++audioEpoch_;
    lastRackFrame_ = -1;
}

void Module::fromJson(json_t* rootJ) {
    restoringModuleJson_ = true;
    try {
        ::rack::engine::Module::fromJson(rootJ);
    }
    catch (...) {
        restoringModuleJson_ = false;
        throw;
    }
    restoringModuleJson_ = false;
    prepareRestoredState();
    publishRestoredState();
}

json_t* Module::dataToJson() {
    json_t* root = json_object();
    json_object_set_new(root, "rvxSchema", json_integer(1));
    appendData(root);
    return root;
}

void Module::dataFromJson(json_t* rootJ) {
    int schema = 0;
    json_t* schemaJ = json_object_get(rootJ, "rvxSchema");
    if (schemaJ && json_is_integer(schemaJ))
        schema = static_cast<int>(json_integer_value(schemaJ));
    readData(rootJ, schema);
    if (!restoringModuleJson_) {
        prepareRestoredState();
        publishRestoredState();
    }
}

void Module::publishRestoredState() {
    for (size_t i = 0; i < params.size() && i < node_->params.size(); ++i)
        node_->params[i].store(params[i].getValue(), std::memory_order_relaxed);
    node_->bypass.store(isBypassed(), std::memory_order_release);
    if (kind_ == Kind::CvBridge)
        ++audioEpoch_;
    lastRackFrame_ = -1;
    incrementTrigger(node_->resets);
}

void Module::appendData(json_t* rootJ) const {
    (void) rootJ;
}

void Module::readData(json_t* rootJ, int schema) {
    (void) rootJ;
    (void) schema;
}

void Module::prepareRestoredState() {
}

VideoPort::VideoPort() {
    box.size = ::rack::mm2px(::rack::math::Vec(7.2f, 7.2f));
}

void VideoPort::bind(const std::shared_ptr<Node>& node, PortType type, int portId) {
    nodeKey_ = node ? node->key : 0;
    videoType_ = type;
    videoPortId_ = portId;
}

void VideoPort::draw(const DrawArgs& args) {
    const ::rack::math::Vec center = box.size.div(2.f);
    NVGcolor domain = videoType_ == PortType::Image
        ? nvgRGB(77, 201, 255) : nvgRGB(255, 90, 194);
    nvgBeginPath(args.vg);
    nvgCircle(args.vg, center.x, center.y, box.size.x * 0.46f);
    nvgFillColor(args.vg, nvgRGB(17, 20, 29));
    nvgFill(args.vg);
    nvgStrokeWidth(args.vg, 2.2f);
    nvgStrokeColor(args.vg, domain);
    nvgStroke(args.vg);

    nvgBeginPath(args.vg);
    nvgCircle(args.vg, center.x, center.y, box.size.x * 0.18f);
    nvgFillColor(args.vg, nvgRGB(3, 5, 9));
    nvgFill(args.vg);
    nvgStrokeWidth(args.vg, 1.f);
    nvgStrokeColor(args.vg, nvgRGBA(255, 255, 255, 100));
    nvgStroke(args.vg);
    ::rack::app::PortWidget::draw(args);
}

ModuleWidget::ModuleWidget(Module* module)
    : rvxModule_(module) {
    setModule(module);
}

void ModuleWidget::step() {
    if (rvxModule_) {
        std::shared_ptr<PatchService> patchService = rvxModule_->service();
        if (patchService)
            patchService->syncRackUi();
    }
    ::rack::app::ModuleWidget::step();
}

VideoPort* ModuleWidget::addVideoInput(::rack::math::Vec position, int portId, PortType type) {
    VideoPort* port = ::rack::createInputCentered<VideoPort>(position, rvxModule_, portId);
    port->bind(rvxModule_ ? rvxModule_->node() : std::shared_ptr<Node>(), type, portId);
    addInput(port);
    return port;
}

VideoPort* ModuleWidget::addVideoOutput(::rack::math::Vec position, int portId, PortType type) {
    VideoPort* port = ::rack::createOutputCentered<VideoPort>(position, rvxModule_, portId);
    port->bind(rvxModule_ ? rvxModule_->node() : std::shared_ptr<Node>(), type, portId);
    addOutput(port);
    return port;
}

} // namespace rackadapter
} // namespace rvx
