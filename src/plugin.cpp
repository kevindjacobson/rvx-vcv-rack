#include "plugin.hpp"

Plugin* pluginInstance = NULL;

void init(Plugin* p) {
    pluginInstance = p;
    p->addModel(modelRvxTestImage);
    p->addModel(modelRvxSignalProcessor);
    p->addModel(modelRvxCvBridge);
    p->addModel(modelRvxFrameDelay);
    p->addModel(modelRvxVideoMonitor);
    p->addModel(modelRvxVideoIo);
    p->addModel(modelRvxVideoRecorder);
}
