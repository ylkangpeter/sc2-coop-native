#include "texture_loader.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#include <iostream>
#include <filesystem>

namespace sc2 {

TextureLoader& TextureLoader::Instance() {
    static TextureLoader instance;
    return instance;
}

TextureLoader::~TextureLoader() {
    Shutdown();
}

void TextureLoader::Init(ID3D11Device* device) {
    m_device = device;
}

void TextureLoader::Shutdown() {
    for (auto& [path, entry] : m_cache) {
        if (entry.srv) {
            entry.srv->Release();
            entry.srv = nullptr;
        }
    }
    m_cache.clear();
    m_device = nullptr;
}

ImTextureID TextureLoader::GetTexture(const std::string& file_path, int* out_width, int* out_height) {
    if (!m_device || file_path.empty()) return (ImTextureID)0;

    auto it = m_cache.find(file_path);
    if (it != m_cache.end()) {
        if (out_width) *out_width = it->second.width;
        if (out_height) *out_height = it->second.height;
        return (ImTextureID)(intptr_t)it->second.srv;
    }

    if (!std::filesystem::exists(file_path)) {
        return (ImTextureID)0;
    }

    int width = 0, height = 0, channels = 0;
    unsigned char* image_data = stbi_load(file_path.c_str(), &width, &height, &channels, 4);
    if (!image_data) {
        return (ImTextureID)0;
    }

    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA subResource;
    subResource.pSysMem = image_data;
    subResource.SysMemPitch = desc.Width * 4;
    subResource.SysMemSlicePitch = 0;

    ID3D11Texture2D* pTexture = nullptr;
    HRESULT hr = m_device->CreateTexture2D(&desc, &subResource, &pTexture);
    stbi_image_free(image_data);

    if (FAILED(hr) || !pTexture) {
        return (ImTextureID)0;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
    ZeroMemory(&srvDesc, sizeof(srvDesc));
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = desc.MipLevels;
    srvDesc.Texture2D.MostDetailedMip = 0;

    ID3D11ShaderResourceView* pSRV = nullptr;
    hr = m_device->CreateShaderResourceView(pTexture, &srvDesc, &pSRV);
    pTexture->Release();

    if (FAILED(hr) || !pSRV) {
        return (ImTextureID)0;
    }

    TextureEntry entry;
    entry.srv = pSRV;
    entry.width = width;
    entry.height = height;
    m_cache[file_path] = entry;

    if (out_width) *out_width = width;
    if (out_height) *out_height = height;

    return (ImTextureID)(intptr_t)pSRV;
}

ImTextureID TextureLoader::GetCommanderIcon(const std::string& commander_name) {
    if (commander_name.empty() || commander_name == "Unknown") return (ImTextureID)0;
    std::string path = "assets/icons/commanders/" + commander_name + ".png";
    return GetTexture(path);
}

ImTextureID TextureLoader::GetMutatorIcon(const std::string& mutator_name) {
    if (mutator_name.empty()) return (ImTextureID)0;
    std::string path = "assets/icons/mutators/" + mutator_name + ".png";
    return GetTexture(path);
}

ImTextureID TextureLoader::GetMapImage(const std::string& map_name) {
    if (map_name.empty()) return (ImTextureID)0;
    std::string path = "assets/maps/" + map_name + ".jpg";
    return GetTexture(path);
}

} // namespace sc2
