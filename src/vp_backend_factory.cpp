/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * 后端工厂实现（注册表模式）
 */

#include <map>
#include <memory>
#include <utility>
#include <vector>
#include <iostream>
#include "vp_backend.h"

namespace SpacemiT {

static std::map<BackendType, VpBackendFactory::FactoryFn>& getRegistry() {
    static std::map<BackendType, VpBackendFactory::FactoryFn> registry;
    return registry;
}

void VpBackendFactory::registerBackend(BackendType type, FactoryFn fn) {
    getRegistry()[type] = std::move(fn);
}

std::unique_ptr<IVpBackend> VpBackendFactory::create(BackendType type) {
    auto& registry = getRegistry();
    auto it = registry.find(type);
    if (it != registry.end()) {
        return it->second();
    }
    std::cerr << "Backend not registered or not available" << std::endl;
    return nullptr;
}

bool VpBackendFactory::isAvailable(BackendType type) {
    return getRegistry().count(type) > 0;
}

std::vector<BackendType> VpBackendFactory::getAvailableBackends() {
    std::vector<BackendType> types;
    for (const auto& [type, _] : getRegistry()) {
        types.push_back(type);
    }
    return types;
}

}  // namespace SpacemiT
