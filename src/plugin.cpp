#include "plugin.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
	pluginInstance = p;
	p->addModel(modelSBV);
	p->addModel(modelSBS);
	p->addModel(modelSBX);
}
