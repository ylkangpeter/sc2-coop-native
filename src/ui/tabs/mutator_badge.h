#pragma once

#include "../../data/cached_mutators.h"
#include "../../data/sc2_constants.h"
#include "../../i18n/i18n.h"
#include "../texture_loader.h"
#include <imgui.h>
#include <cstring>
#include <string>

namespace sc2 {

inline void RenderMutatorBadge(const char* mut, bool show_points = true, bool icon_only = false) {
    ImGui::BeginGroup();
    ImTextureID icon = TextureLoader::Instance().GetMutatorIcon(mut);
    if (icon) {
        ImGui::Image(icon, ImVec2(18, 18));
        if (!icon_only) ImGui::SameLine();
    } else if (icon_only) {
        ImGui::TextDisabled("?");
    }
    int pts = GetMutatorPoint(mut);
    if (!icon_only && show_points && pts >= 0) {
        ImGui::Text("%s (%d %s)", TR(mut), pts, TR("pts"));
    } else if (!icon_only) {
        ImGui::Text("%s", TR(mut));
    }
    ImGui::EndGroup();
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        if (pts >= 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s  [%d %s]", TR(mut), pts, TR("pts"));
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", TR(mut));
        }
        ImGui::Separator();
        std::string desc_key = std::string("mut_desc_") + mut;
        const char* localized_desc = TR(desc_key.c_str());
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
        if (std::strcmp(localized_desc, desc_key.c_str()) != 0) {
            ImGui::TextUnformatted(localized_desc);
        } else {
            auto desc_it = kMutators.find(mut);
            if (desc_it != kMutators.end()) {
                ImGui::TextUnformatted(desc_it->second.c_str());
            } else {
                ImGui::TextDisabled("%s", TR("Special event mutator"));
            }
        }
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}


} // namespace sc2
