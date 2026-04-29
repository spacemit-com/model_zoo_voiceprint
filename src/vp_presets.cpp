/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * VpConfig 预设定义
 */

#include <map>
#include <string>
#include <vector>
#include <functional>
#include <stdexcept>
#include "../include/vp_service.h"

namespace SpacemiT {

static const std::map<std::string, std::function<VpConfig()>>& getPresets() {
    static const std::map<std::string, std::function<VpConfig()>> presets = {
        {"campplus", []() {
            VpConfig c;
            c.backend = VpBackendType::CAMPPLUS;
            c.model_dir = "~/.cache/models/vp/campplus";
            return c;
        }},
    };
    return presets;
}

VpConfig VpConfig::Preset(const std::string& name) {
    const auto& presets = getPresets();
    auto it = presets.find(name);
    if (it == presets.end()) {
        throw std::invalid_argument("Unknown voiceprint preset: '" + name + "'");
    }
    return it->second();
}

std::vector<std::string> VpConfig::AvailablePresets() {
    std::vector<std::string> names;
    for (const auto& [name, _] : getPresets()) {
        names.push_back(name);
    }
    return names;
}

}  // namespace SpacemiT
