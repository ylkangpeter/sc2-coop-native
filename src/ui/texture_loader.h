#pragma once

#include <d3d11.h>
#include <string>
#include <unordered_map>
#include <imgui.h>

namespace sc2 {

class TextureLoader {
public:
    static TextureLoader& Instance();

    void Init(ID3D11Device* device);
    void Shutdown();

    // Get or load texture from relative or absolute file path
    ImTextureID GetTexture(const std::string& file_path, int* out_width = nullptr, int* out_height = nullptr);

    // Get commander icon by commander name
    ImTextureID GetCommanderIcon(const std::string& commander_name);

    // Get mutator icon by mutator name
    ImTextureID GetMutatorIcon(const std::string& mutator_name);

    // Get map thumbnail by map name
    ImTextureID GetMapImage(const std::string& map_name);

private:
    TextureLoader() = default;
    ~TextureLoader();

    struct TextureEntry {
        ID3D11ShaderResourceView* srv = nullptr;
        int width = 0;
        int height = 0;
    };

    ID3D11Device* m_device = nullptr;
    std::unordered_map<std::string, TextureEntry> m_cache;
};

} // namespace sc2
