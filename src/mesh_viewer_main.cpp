// Explicit inspection of a local 3DG group or selected 3DD instances.
#include "tdur/assets/bank_archive.hpp"
#include "tdur/assets/geometry_3dg.hpp"
#include "tdur/assets/material_textures.hpp"
#include "tdur/assets/scene_mesh.hpp"
#include "tdur/render/d3d12/mesh_viewer.hpp"

#include <DirectXMath.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>

using namespace tdur::render::d3d12;

namespace {
struct Options {
    std::filesystem::path bank;
    std::filesystem::path output_bmp;
    std::optional<uint32_t> member;
    std::optional<uint32_t> scene_member;
    std::optional<uint32_t> material_member;
    std::optional<uint32_t> texture_stage;
    std::vector<uint32_t> texture_members;
    std::optional<tdur::assets::Material2DMKey> only_material;
    std::optional<std::array<float, 4>> border_color;
    std::vector<tdur::assets::SceneMeshSelection> nodes;
    std::string group = "BODY_HR";
    uint32_t seconds = 5;
    uint32_t size = 512;
    float yaw = 35;
    float pitch = 20;
    bool warp = false;
    bool debug = false;
    bool headless = false;
    bool list_nodes = false;
    bool group_explicit = false;
};

std::string Ascii(std::wstring_view text) {
    std::string result;
    for (const auto value : text) {
        if (value < 0x20 || value > 0x7e) {
            throw std::invalid_argument(
                "Expected printable ASCII option value");
        }
        result.push_back(char(value));
    }
    return result;
}

template <class Value> Value Number(std::wstring_view text) {
    const auto ascii = Ascii(text);
    Value value{};
    const auto [end, error] =
        std::from_chars(ascii.data(), ascii.data() + ascii.size(), value);
    if (ascii.empty() || error != std::errc{} ||
        end != ascii.data() + ascii.size()) {
        throw std::invalid_argument("Expected bounded decimal option value");
    }
    return value;
}

tdur::assets::Material2DMKey BinaryKey(std::wstring_view text) {
    if (text.size() != 16)
        throw std::invalid_argument(
            "Material key requires exactly 16 hex digits");
    const auto digit = [](wchar_t c) -> unsigned {
        if (c >= L'0' && c <= L'9')
            return unsigned(c - L'0');
        if (c >= L'a' && c <= L'f')
            return unsigned(c - L'a') + 10;
        if (c >= L'A' && c <= L'F')
            return unsigned(c - L'A') + 10;
        throw std::invalid_argument("Material key contains a non-hex digit");
    };
    tdur::assets::Material2DMKey result{};
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] =
            std::byte((digit(text[i * 2]) << 4) | digit(text[i * 2 + 1]));
    return result;
}

std::array<float, 4> BorderColor(std::wstring_view text) {
    std::array<float, 4> result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        const auto comma = text.find(L',');
        if ((i < 3 && comma == std::wstring_view::npos) ||
            (i == 3 && comma != std::wstring_view::npos))
            throw std::invalid_argument("Border color requires R,G,B,A");
        result[i] = Number<float>(text.substr(0, comma));
        if (!std::isfinite(result[i]) || result[i] < 0 || result[i] > 1)
            throw std::invalid_argument(
                "Border components must be finite in [0,1]");
        if (comma != std::wstring_view::npos)
            text.remove_prefix(comma + 1);
    }
    return result;
}

Options Parse(int argc, wchar_t** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view argument(argv[i]);
        if (argument == L"--headless") {
            options.headless = true;
        } else if (argument == L"--warp") {
            options.warp = true;
        } else if (argument == L"--debug") {
            options.debug = true;
        } else if (argument == L"--list-nodes") {
            options.list_nodes = true;
        } else {
            if (i + 1 >= argc) {
                throw std::invalid_argument("Missing mesh-viewer option value");
            }
            const std::wstring_view value(argv[++i]);
            if (argument == L"--bank") {
                options.bank = value;
            } else if (argument == L"--output-bmp") {
                options.output_bmp = value;
            } else if (argument == L"--member") {
                options.member = Number<uint32_t>(value);
            } else if (argument == L"--group") {
                options.group = Ascii(value);
                options.group_explicit = true;
            } else if (argument == L"--scene-member") {
                options.scene_member = Number<uint32_t>(value);
            } else if (argument == L"--material-member") {
                options.material_member = Number<uint32_t>(value);
            } else if (argument == L"--texture-stage") {
                options.texture_stage = Number<uint32_t>(value);
            } else if (argument == L"--texture-member") {
                options.texture_members.push_back(Number<uint32_t>(value));
            } else if (argument == L"--only-material") {
                options.only_material = BinaryKey(value);
            } else if (argument == L"--border-color") {
                options.border_color = BorderColor(value);
            } else if (argument == L"--node") {
                const auto colon = value.find(L':');
                const auto index = Number<std::size_t>(value.substr(0, colon));
                std::optional<std::size_t> entry;
                if (colon != std::wstring_view::npos) {
                    entry = Number<std::size_t>(value.substr(colon + 1));
                }
                options.nodes.push_back({index, entry});
            } else if (argument == L"--seconds") {
                options.seconds = Number<uint32_t>(value);
            } else if (argument == L"--size") {
                options.size = Number<uint32_t>(value);
            } else if (argument == L"--yaw") {
                options.yaw = Number<float>(value);
            } else if (argument == L"--pitch") {
                options.pitch = Number<float>(value);
            } else {
                throw std::invalid_argument("Unknown mesh-viewer option");
            }
        }
    }
    if ((options.scene_member &&
         (options.group_explicit ||
          (options.nodes.empty() && !options.list_nodes) ||
          (!options.nodes.empty() && options.list_nodes) ||
          (options.list_nodes && !options.output_bmp.empty()))) ||
        (!options.scene_member &&
         (!options.nodes.empty() || options.list_nodes))) {
        throw std::invalid_argument(
            "Use scene-member with explicit node selections or list-nodes; "
            "group selects local geometry only");
    }
    if (options.material_member) {
        if (!options.scene_member || options.list_nodes ||
            !options.texture_stage || *options.texture_stage > 7 ||
            options.texture_members.empty() ||
            options.texture_members.size() > 256)
            throw std::invalid_argument(
                "Material inspection needs scene nodes, an explicit "
                "texture-stage 0..7 "
                "and 1..256 texture-member entries in loading order");
    } else if (options.texture_stage || !options.texture_members.empty() ||
               options.only_material || options.border_color) {
        throw std::invalid_argument("Texture options require material-member");
    }
    if (options.bank.empty() || !options.member || options.group.empty() ||
        options.group.size() > 8 || !options.seconds || options.seconds > 60 ||
        options.size < 64 || options.size > 2048 ||
        !std::isfinite(options.yaw) || std::abs(options.yaw) > 360 ||
        !std::isfinite(options.pitch) || std::abs(options.pitch) > 80) {
        throw std::invalid_argument(
            "Provide bank/member; group 1..8 bytes, size 64..2048, seconds "
            "1..60, yaw +/-360, pitch +/-80");
    }
    return options;
}

struct InspectionMesh {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    std::array<float, 3> minimum;
    std::array<float, 3> maximum;
    uint32_t primitives{};
    uint32_t unique_groups = 1;
    bool scene_placed = false;
    bool textured = false;
    uint32_t filtered_primitive_draws{};
    std::vector<MeshDraw> draws;
    struct MaterialDraw {
        std::size_t node_index{}, object_entry_index{}, primitive_index{},
            material_index{};
        uint32_t texture_index{};
        tdur::assets::Material2DMKey material_key{};
        tdur::assets::Texture2DBKey texture_key{};
        std::array<std::uint8_t, 4> sampler_indices{};
    };
    std::vector<MaterialDraw> material_draws;
};

struct InspectionTextures {
    struct Texture {
        std::vector<tdur::assets::Texture2DBRGBA8> decoded;
        std::vector<MeshTextureLevel> levels;
    };
    std::vector<Texture> storage;
    std::vector<MeshTexture> gpu;
    std::vector<tdur::assets::MaterialTextureResource> resources;
};

InspectionTextures LoadTextures(const tdur::assets::BankArchive& bank,
                                std::span<const uint32_t> members) {
    InspectionTextures result;
    result.storage.reserve(members.size());
    uint64_t decoded_bytes{};
    std::size_t total_mips{};
    for (const auto member : members) {
        const auto bytes = bank.stored_bytes(member);
        if (!bytes)
            throw std::runtime_error("Texture member: " + bytes.error().detail);
        const auto texture = tdur::assets::Texture2DBView::parse(*bytes);
        if (!texture)
            throw std::runtime_error("Texture2DB: " + texture.error().detail);
        if (texture->slice_count() != 1 || texture->mip_count() > 15 ||
            total_mips + texture->mip_count() > 1024)
            throw std::runtime_error("Inspection supports bounded 2D mip "
                                     "chains; cube sampling is unresolved");
        total_mips += texture->mip_count();
        InspectionTextures::Texture owned;
        owned.decoded.reserve(texture->mip_count());
        for (uint32_t mip = 0; mip < texture->mip_count(); ++mip) {
            const auto subresource = texture->subresource(mip);
            if (!subresource)
                throw std::runtime_error("Texture subresource: " +
                                         subresource.error().detail);
            const auto amount =
                uint64_t(subresource->width) * subresource->height * 4;
            constexpr uint64_t maximum = 256ULL * 1024 * 1024;
            if (amount > maximum - decoded_bytes)
                throw std::runtime_error(
                    "Texture collection exceeds the decoded RGBA byte budget");
            decoded_bytes += amount;
            auto image = texture->decode_rgba8(mip);
            if (!image)
                throw std::runtime_error("Texture pixels: " +
                                         image.error().detail);
            owned.decoded.push_back(std::move(*image));
        }
        result.resources.push_back(
            {texture->resource_key(), result.storage.size()});
        result.storage.push_back(std::move(owned));
    }
    // Build borrowed upload spans only after all owned mip vectors are stable.
    result.gpu.reserve(result.storage.size());
    for (auto& texture : result.storage) {
        texture.levels.reserve(texture.decoded.size());
        for (const auto& image : texture.decoded)
            texture.levels.push_back({image.width, image.height, image.pixels});
        result.gpu.push_back({texture.levels});
    }
    return result;
}

MeshSampler Sampler(const tdur::assets::MaterialTextureSampler& source,
                    const Options& options) {
    using namespace tdur::assets;
    MeshSampler result;
    switch (source.min_mag) {
    case MaterialMinMagFilter::point:
        result.min_filter = result.mag_filter = MeshTextureFilter::point;
        break;
    case MaterialMinMagFilter::linear:
        result.min_filter = result.mag_filter = MeshTextureFilter::linear;
        break;
    default:
        throw std::runtime_error("Unsupported material MIN/MAG filter");
    }
    switch (source.mip) {
    case MaterialMipFilter::none:
        result.mip_filter = MeshMipFilter::none;
        break;
    case MaterialMipFilter::linear:
        result.mip_filter = MeshMipFilter::linear;
        break;
    default:
        throw std::runtime_error("Unsupported material MIP filter");
    }
    const auto address = [](MaterialAddressMode mode) {
        switch (mode) {
        case MaterialAddressMode::wrap:
            return MeshAddressMode::wrap;
        case MaterialAddressMode::clamp:
            return MeshAddressMode::clamp;
        case MaterialAddressMode::mirror:
            return MeshAddressMode::mirror;
        case MaterialAddressMode::border:
            return MeshAddressMode::border;
        default:
            throw std::runtime_error("Unsupported material address mode");
        }
    };
    result.address_u = address(source.address_u);
    result.address_v = address(source.address_v);
    if ((source.address_u == MaterialAddressMode::border ||
         source.address_v == MaterialAddressMode::border) &&
        !options.border_color)
        throw std::runtime_error("Material requests BORDER; supply an explicit "
                                 "inspection border-color");
    if (options.border_color)
        result.border_rgba = *options.border_color;
    return result;
}

InspectionMesh Combine(const tdur::assets::Geometry3DGGroup& group) {
    InspectionMesh result;
    result.minimum.fill(std::numeric_limits<float>::max());
    result.maximum.fill(std::numeric_limits<float>::lowest());
    for (const auto& primitive : group.primitives) {
        if (result.vertices.size() + primitive.positions.size() > 2'000'000 ||
            result.indices.size() + primitive.triangle_indices.size() >
                12'000'000) {
            throw std::runtime_error(
                "Selected group exceeds inspection buffer limits");
        }
        const auto base = uint32_t(result.vertices.size());
        for (const auto& position : primitive.positions) {
            result.vertices.push_back({position, {0.55f, 0.72f, 0.88f, 1}});
            for (size_t axis = 0; axis < 3; ++axis) {
                result.minimum[axis] =
                    std::min(result.minimum[axis], position[axis]);
                result.maximum[axis] =
                    std::max(result.maximum[axis], position[axis]);
            }
        }
        for (const auto index : primitive.triangle_indices) {
            result.indices.push_back(base + index);
        }
        ++result.primitives;
    }
    if (result.vertices.empty() || result.indices.empty()) {
        throw std::runtime_error("Selected group has no supported triangles");
    }
    result.draws.push_back({0, uint32_t(result.indices.size())});
    return result;
}

InspectionMesh
CombineScene(const tdur::assets::Scene3DD& scene,
             std::span<const std::byte> geometry,
             std::span<const tdur::assets::SceneMeshSelection> selection) {
    const auto packed =
        tdur::assets::SceneMesh::build_selected(scene, geometry, selection);
    if (!packed) {
        throw std::runtime_error("SceneMesh: " + packed.error().detail);
    }
    if (!packed->bounds()) {
        throw std::runtime_error("Select at least one drawable scene node");
    }
    InspectionMesh result;
    result.vertices.reserve(packed->vertices().size());
    for (const auto& vertex : packed->vertices()) {
        result.vertices.push_back({vertex.position, {0.55f, 0.72f, 0.88f, 1}});
    }
    result.indices.assign(packed->indices().begin(), packed->indices().end());
    result.minimum = packed->bounds()->minimum;
    result.maximum = packed->bounds()->maximum;
    result.unique_groups = uint32_t(packed->groups().size());
    result.scene_placed = true;
    for (const auto& group : packed->groups()) {
        result.primitives += uint32_t(group.primitives.size());
    }
    for (const auto& instance : packed->instances()) {
        const auto& group = packed->groups()[instance.group_index];
        result.draws.push_back({group.first_index, group.index_count,
                                instance.composed_transform});
    }
    return result;
}

void IncludePrimitiveBounds(InspectionMesh& result,
                            const tdur::assets::SceneMesh& packed,
                            const tdur::assets::SceneMeshPrimitive& primitive,
                            const tdur::assets::Scene3DDMatrix& transform) {
    // Match SceneMesh's row-vector/positive-W convention, but restrict the
    // inspection camera to primitives retained by the explicit material filter.
    // Its existing transformed-vertex budget also bounds this subset's work.
    for (const auto& vertex : packed.vertices().subspan(
             primitive.first_vertex, primitive.vertex_count)) {
        std::array<double, 4> placed{};
        for (std::size_t column = 0; column < 4; ++column) {
            placed[column] = transform[12 + column];
            for (std::size_t axis = 0; axis < 3; ++axis)
                placed[column] += double(vertex.position[axis]) *
                                  transform[axis * 4 + column];
        }
        if (!std::isfinite(placed[3]) || placed[3] <= 0)
            throw std::runtime_error(
                "Retained primitive has invalid homogeneous W");
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const double position = placed[axis] / placed[3];
            if (!std::isfinite(position) ||
                std::abs(position) > std::numeric_limits<float>::max())
                throw std::runtime_error(
                    "Retained primitive bounds exceed float range");
            result.minimum[axis] =
                std::min(result.minimum[axis], float(position));
            result.maximum[axis] =
                std::max(result.maximum[axis], float(position));
        }
    }
}

InspectionMesh
CombineTexturedScene(const tdur::assets::Scene3DD& scene,
                     std::span<const std::byte> geometry,
                     const tdur::assets::Material2DM& materials,
                     const tdur::assets::MaterialTextureBindings& textures,
                     const Options& options) {
    const auto packed =
        tdur::assets::SceneMesh::build_selected(scene, geometry, options.nodes);
    if (!packed || !packed->bounds())
        throw std::runtime_error(packed
                                     ? "Select at least one drawable scene node"
                                     : "SceneMesh: " + packed.error().detail);
    InspectionMesh result;
    result.minimum.fill(std::numeric_limits<float>::max());
    result.maximum.fill(std::numeric_limits<float>::lowest());
    result.scene_placed = result.textured = true;
    result.unique_groups = uint32_t(packed->groups().size());
    result.vertices.reserve(packed->vertices().size());
    for (const auto& vertex : packed->vertices())
        result.vertices.push_back(
            {vertex.position, {1, 1, 1, 1}, vertex.texcoord});
    result.indices.assign(packed->indices().begin(), packed->indices().end());
    for (const auto& group : packed->groups())
        result.primitives += uint32_t(group.primitives.size());
    for (const auto& instance : packed->instances()) {
        const auto& group = packed->groups()[instance.group_index];
        for (std::size_t i = 0; i < group.primitives.size(); ++i) {
            const auto& primitive = group.primitives[i];
            const auto material = materials.bind_primitive(
                scene, instance.source_material_bank_index,
                primitive.opaque_binding_word);
            if (!material)
                throw std::runtime_error("Primitive material: " +
                                         material.error().detail);
            const auto& record =
                materials.materials()[material->material_index];
            if (options.only_material && record.key != *options.only_material) {
                ++result.filtered_primitive_draws;
                continue;
            }
            if (!primitive.has_texcoords)
                throw std::runtime_error(
                    "Selected textured primitive has no established UVs");
            const auto plan = textures.plan(material->material_index);
            if (!plan || *options.texture_stage >= plan->stage_count)
                throw std::runtime_error(plan ? "Selected texture stage is not "
                                                "active in this material"
                                              : "Material textures: " +
                                                    plan.error().detail);
            const auto& stage = plan->stages[*options.texture_stage];
            if (stage.action !=
                    tdur::assets::MaterialTextureAction::bind_cached ||
                !stage.texture_token || *stage.texture_token > UINT32_MAX)
                throw std::runtime_error(
                    "Selected stage is unresolved or preserves prior texture "
                    "state; no inspection substitute supplied");
            MeshDraw draw{primitive.first_index, primitive.index_count,
                          instance.composed_transform};
            draw.texture_index = static_cast<uint32_t>(*stage.texture_token);
            draw.sampler = Sampler(stage.sampler, options);
            result.draws.push_back(draw);
            IncludePrimitiveBounds(result, *packed, primitive,
                                   instance.composed_transform);
            const auto& layout = textures.layouts()[material->material_index];
            const auto& slot = textures.layers()[layout.layer_index]
                                   .slots[*options.texture_stage];
            result.material_draws.push_back(
                {instance.source_node_index, instance.source_object_entry_index,
                 i, material->material_index, *draw.texture_index, record.key,
                 slot.key, slot.sampler_indices});
        }
    }
    if (result.draws.empty())
        throw std::runtime_error("Material filter selected no primitive draws");
    return result;
}

std::array<float, 16> Camera(const InspectionMesh& mesh, uint32_t width,
                             uint32_t height, float yawDegrees,
                             float pitchDegrees) {
    using namespace DirectX;
    std::array<float, 3> center{};
    double squared = 0;
    for (size_t axis = 0; axis < 3; ++axis) {
        center[axis] =
            float((double(mesh.minimum[axis]) + mesh.maximum[axis]) * 0.5);
        const double half =
            (double(mesh.maximum[axis]) - mesh.minimum[axis]) * 0.5;
        squared += half * half;
    }
    const double radiusDouble = std::max(0.001, std::sqrt(squared));
    if (radiusDouble > std::numeric_limits<float>::max() / 16.0) {
        throw std::runtime_error(
            "Group bounds exceed finite inspection-camera range");
    }
    const float radius = float(radiusDouble);
    constexpr float fov = XM_PIDIV4;
    const float aspect = float(width) / height;
    const float horizontal = 2 * std::atan(std::tan(fov * 0.5f) * aspect);
    const float distance =
        radius * 1.15f / std::sin(std::min(fov, horizontal) * 0.5f);
    const float yaw = yawDegrees * XM_PI / 180;
    const float pitch = pitchDegrees * XM_PI / 180;
    const auto focus = XMVectorSet(center[0], center[1], center[2], 0);
    const auto direction =
        XMVectorSet(std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                    std::cos(yaw) * std::cos(pitch), 0);
    const auto eye = XMVectorAdd(focus, XMVectorScale(direction, distance));
    const auto view = XMMatrixLookAtLH(eye, focus, XMVectorSet(0, 1, 0, 0));
    const auto projection = XMMatrixPerspectiveFovLH(
        fov, aspect, radius * 0.01f, distance + radius * 4);
    XMFLOAT4X4 stored;
    XMStoreFloat4x4(&stored, view * projection);
    std::array<float, 16> matrix;
    static_assert(sizeof(stored) == sizeof(matrix));
    std::memcpy(matrix.data(), &stored, sizeof(stored));
    return matrix;
}

uint32_t Coverage(const MeshReadback& image) {
    uint32_t result = 0;
    for (size_t i = 0; i < image.pixels.size(); i += 4) {
        result += image.pixels[i] != std::byte{0} ||
                  image.pixels[i + 1] != std::byte{0} ||
                  image.pixels[i + 2] != std::byte{0};
    }
    return result;
}

std::vector<MeshDraw> Draws(const InspectionMesh& mesh, uint32_t width,
                            uint32_t height, const Options& options) {
    const auto camera = Camera(mesh, width, height, options.yaw, options.pitch);
    auto draws = mesh.draws;
    for (auto& draw : draws) {
        std::array<float, 16> combined{};
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t col = 0; col < 4; ++col) {
                double value = 0;
                for (std::size_t k = 0; k < 4; ++k) {
                    value += double(draw.transform[row * 4 + k]) *
                             camera[k * 4 + col];
                }
                if (!std::isfinite(value) ||
                    std::abs(value) > std::numeric_limits<float>::max()) {
                    throw std::runtime_error(
                        "Scene-to-clip matrix exceeds float range");
                }
                combined[row * 4 + col] = float(value);
            }
        }
        draw.transform = combined;
    }
    return draws;
}

uint64_t Fingerprint(std::span<const std::byte> bytes) {
    uint64_t result = 14695981039346656037ULL;
    for (const auto value : bytes) {
        result = (result ^ std::to_integer<uint8_t>(value)) * 1099511628211ULL;
    }
    return result;
}

void SaveReadback(const std::filesystem::path& path,
                  const MeshReadback& image) {
    if (path.empty())
        return;
    BITMAPFILEHEADER file{};
    BITMAPINFOHEADER info{};
    static_assert(sizeof(file) == 14 && sizeof(info) == 40);
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof(file) + sizeof(info);
    file.bfSize = file.bfOffBits + DWORD(image.pixels.size());
    info.biSize = sizeof(info);
    info.biWidth = LONG(image.width);
    info.biHeight = -LONG(image.height);
    info.biPlanes = 1;
    info.biBitCount = 32;
    info.biCompression = BI_RGB;
    info.biSizeImage = DWORD(image.pixels.size());
    std::vector<std::byte> bitmap(file.bfSize);
    std::memcpy(bitmap.data(), &file, sizeof(file));
    std::memcpy(bitmap.data() + sizeof(file), &info, sizeof(info));
    for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
        bitmap[file.bfOffBits + i] = image.pixels[i + 2];
        bitmap[file.bfOffBits + i + 1] = image.pixels[i + 1];
        bitmap[file.bfOffBits + i + 2] = image.pixels[i];
        bitmap[file.bfOffBits + i + 3] = image.pixels[i + 3];
    }
    const auto handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                    CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        Check(HRESULT_FROM_WIN32(GetLastError()), "Create new inspection BMP");
    }
    DWORD written = 0;
    const auto success = WriteFile(handle, bitmap.data(), DWORD(bitmap.size()),
                                   &written, nullptr);
    const auto error = success ? ERROR_WRITE_FAULT : GetLastError();
    CloseHandle(handle);
    if (!success || written != bitmap.size()) {
        Check(HRESULT_FROM_WIN32(error), "Write inspection BMP");
    }
}

void Quoted(std::string_view text) {
    std::cout << '"';
    for (const auto value : text) {
        const auto byte = static_cast<unsigned char>(value);
        if (byte == '"' || byte == '\\') {
            std::cout << '\\' << value;
        } else if (byte < 0x20 || byte >= 0x7f) {
            std::cout << "\\u00" << std::hex << std::setw(2)
                      << std::setfill('0') << unsigned(byte) << std::dec
                      << std::setfill(' ');
        } else {
            std::cout << value;
        }
    }
    std::cout << '"';
}

void ListNodes(const tdur::assets::Scene3DD& scene,
               std::span<const std::byte> geometry) {
    const auto bindings = scene.resolve_geometry(geometry);
    if (!bindings) {
        throw std::runtime_error("Scene bindings: " + bindings.error().detail);
    }
    std::cout << "{\"scope\":\"3dd-reference-list\",\"game_executed\":false,"
                 "\"nodes\":[";
    for (std::size_t i = 0; i < scene.nodes().size(); ++i) {
        const auto& node = scene.nodes()[i];
        if (i)
            std::cout << ',';
        std::cout << "{\"index\":" << i << ",\"key_hex\":\"";
        for (const auto byte : node.key) {
            std::cout << std::hex << std::setw(2) << std::setfill('0')
                      << std::to_integer<unsigned>(byte);
        }
        std::cout << std::dec << std::setfill(' ') << "\",\"parent_index\":";
        if (node.parent_index)
            std::cout << *node.parent_index;
        else
            std::cout << "null";
        std::cout << ",\"drawable\":" << (node.object_index ? "true" : "false")
                  << ",\"object_entries\":[";
        for (std::size_t entry_index = 0;
             entry_index < node.object_entries.size(); ++entry_index) {
            if (entry_index)
                std::cout << ',';
            const auto& entry = node.object_entries[entry_index];
            std::cout << "{\"index\":" << entry_index
                      << ",\"object_key_hex\":\"";
            for (const auto byte : scene.objects()[entry.object_index].key) {
                std::cout << std::hex << std::setw(2) << std::setfill('0')
                          << std::to_integer<unsigned>(byte);
            }
            std::cout << std::dec << std::setfill(' ')
                      << "\",\"opaque_words\":[";
            for (std::size_t word = 0; word < entry.opaque_words.size();
                 ++word) {
                if (word)
                    std::cout << ',';
                std::cout << entry.opaque_words[word];
            }
            std::cout << "]}";
        }
        std::cout << "]}";
    }
    std::cout << "]}\n";
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM word,
                                 LPARAM data) {
    if (message == WM_CLOSE || (message == WM_KEYDOWN && word == VK_ESCAPE)) {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, word, data);
}

class InspectionWindow {
  public:
    InspectionWindow() {
        WNDCLASSW description{};
        description.lpfnWndProc = WindowProcedure;
        description.hInstance = GetModuleHandleW(nullptr);
        description.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        description.lpszClassName = ClassName;
        if (!RegisterClassW(&description)) {
            Check(HRESULT_FROM_WIN32(GetLastError()),
                  "Register 3DG inspection window");
        }
        RECT bounds{0, 0, 640, 640};
        if (!AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, FALSE)) {
            Check(HRESULT_FROM_WIN32(GetLastError()), "Adjust 3DG window");
        }
        window_ = CreateWindowExW(
            0, ClassName, L"TDU-Recompilation | Ressources 3DG/3DD D3D12",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
            bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr,
            nullptr, description.hInstance, nullptr);
        if (!window_) {
            Check(HRESULT_FROM_WIN32(GetLastError()),
                  "Create 3DG inspection window");
        }
    }
    ~InspectionWindow() {
        if (window_ && IsWindow(window_)) {
            DestroyWindow(window_);
        }
        UnregisterClassW(ClassName, GetModuleHandleW(nullptr));
    }
    HWND get() const { return window_; }
    InspectionWindow(const InspectionWindow&) = delete;
    InspectionWindow& operator=(const InspectionWindow&) = delete;

  private:
    static constexpr wchar_t ClassName[] = L"TDUR_Native_3DG_Group_Viewer";
    HWND window_ = nullptr;
};

uint32_t Present(NativeDevice& native, MeshViewer& viewer,
                 const InspectionMesh& mesh, const Options& options) {
    InspectionWindow window;
    RECT client{};
    if (!GetClientRect(window.get(), &client)) {
        Check(HRESULT_FROM_WIN32(GetLastError()), "Read 3DG client size");
    }
    uint32_t width = uint32_t(client.right);
    uint32_t height = uint32_t(client.bottom);
    native.createSwapchain(window.get(), width, height);
    ShowWindow(window.get(), SW_SHOW);
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(options.seconds);
    uint32_t frames = 0;
    bool running = true;
    while (running && std::chrono::steady_clock::now() < deadline) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                running = false;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!running || !IsWindow(window.get())) {
            break;
        }
        if (!GetClientRect(window.get(), &client)) {
            Check(HRESULT_FROM_WIN32(GetLastError()),
                  "Read resized 3DG window");
        }
        const uint32_t newWidth = uint32_t(client.right);
        const uint32_t newHeight = uint32_t(client.bottom);
        if (!newWidth || !newHeight) {
            Sleep(10);
            continue;
        }
        if (newWidth != width || newHeight != height) {
            native.resizeSwapchain(newWidth, newHeight);
            width = newWidth;
            height = newHeight;
        }
        const auto draws = Draws(mesh, width, height, options);
        // Each slot waits only when reused; the loop drains before closing.
        (void)viewer.present_async(draws, !mesh.textured);
        ++frames;
    }
    native.waitForIdle();
    return frames;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring_view(argv[1]) == L"--help") {
        std::cout
            << "TDU-Recompilation - native D3D12 selected-resource "
               "inspection\n"
               "Usage: tdur_view_mesh --bank <PC-KNAB-file> --member "
               "<index> [--group BODY_HR]\n"
               "       [--headless] [--size 64..2048] [--seconds 1..60] "
               "[--yaw degrees] [--pitch degrees] [--warp] [--debug]\n"
               "       [--output-bmp <new-file>] exports this renderer's "
               "readback only.\n"
               "       or --scene-member <index> --node <3DD-index[:entry]> "
               "[--node <index> ...], or --scene-member <index> --list-nodes\n"
               "       Textured inspection: --material-member <2DM-index> "
               "--texture-stage <0..7> --texture-member <2DB-index> "
               "[--texture-member <index> ...]\n"
               "       [--only-material <16-hex-key>] "
               "[--border-color R,G,B,A]. Unresolved stages fail explicitly.\n"
               "Monochrome inspection camera; local group or explicit 3DD "
               "placement; textures use the selected raw stage, not original "
               "shaders.\n";
        return 0;
    }
    try {
        const auto options = Parse(argc, argv);
        const auto bank = tdur::assets::BankArchive::read_file(options.bank);
        if (!bank) {
            throw std::runtime_error("PC KNAB: " + bank.error().detail);
        }
        const auto bytes = bank->stored_bytes(*options.member);
        if (!bytes) {
            throw std::runtime_error("Bank member: " + bytes.error().detail);
        }
        InspectionMesh mesh;
        InspectionTextures texture_upload;
        if (options.scene_member) {
            const auto scene_bytes = bank->stored_bytes(*options.scene_member);
            if (!scene_bytes) {
                throw std::runtime_error("Scene member: " +
                                         scene_bytes.error().detail);
            }
            const auto scene = tdur::assets::Scene3DD::parse(*scene_bytes);
            if (!scene) {
                throw std::runtime_error("Scene3DD: " + scene.error().detail);
            }
            if (options.list_nodes) {
                ListNodes(*scene, *bytes);
                return 0;
            }
            if (options.material_member) {
                const auto material_bytes =
                    bank->stored_bytes(*options.material_member);
                if (!material_bytes)
                    throw std::runtime_error("Material member: " +
                                             material_bytes.error().detail);
                const auto materials =
                    tdur::assets::Material2DM::parse(*material_bytes);
                if (!materials)
                    throw std::runtime_error("Material2DM: " +
                                             materials.error().detail);
                auto textures =
                    tdur::assets::MaterialTextureBindings::from_materials(
                        *materials);
                if (!textures)
                    throw std::runtime_error("Material textures: " +
                                             textures.error().detail);
                texture_upload = LoadTextures(*bank, options.texture_members);
                const auto rebound = textures->rebind(texture_upload.resources);
                if (!rebound)
                    throw std::runtime_error("Texture rebind: " +
                                             rebound.error().detail);
                mesh = CombineTexturedScene(*scene, *bytes, *materials,
                                            *textures, options);
            } else {
                mesh = CombineScene(*scene, *bytes, options.nodes);
            }
        } else {
            const auto geometry = tdur::assets::Geometry3DG::parse(*bytes);
            if (!geometry) {
                throw std::runtime_error("Geometry3DG: " +
                                         geometry.error().detail);
            }
            const auto group = std::find_if(
                geometry->groups().begin(), geometry->groups().end(),
                [&](const auto& candidate) {
                    return candidate.name_bytes == options.group;
                });
            if (group == geometry->groups().end()) {
                throw std::runtime_error("Named 3DG group not found");
            }
            mesh = Combine(*group);
        }
        NativeDevice native;
        native.initialize(options.warp, options.debug);
        MeshViewer viewer(native);
        viewer.upload(mesh.vertices, mesh.indices);
        if (mesh.textured)
            viewer.upload_textures(texture_upload.gpu);
        const auto draws = Draws(mesh, options.size, options.size, options);
        const auto image = viewer.render_readback(options.size, options.size,
                                                  draws, !mesh.textured);
        const auto covered = Coverage(image);
        auto geometry_covered = covered;
        if (mesh.textured) {
            // A black texture is valid. Measure the same geometry separately
            // with its white vertex colors, rather than inferring draw coverage
            // from a texture's RGB values. The requested readback is unchanged.
            auto geometry_draws = draws;
            for (auto& draw : geometry_draws)
                draw.texture_index.reset();
            geometry_covered = Coverage(viewer.render_readback(
                options.size, options.size, geometry_draws, false));
        }
        if (geometry_covered < 64) {
            throw std::runtime_error("Inspection camera produced insufficient "
                                     "non-background coverage");
        }
        SaveReadback(options.output_bmp, image);
        const auto frames =
            options.headless ? 0 : Present(native, viewer, mesh, options);
        if (native.validationErrorCount() != 0) {
            throw std::runtime_error("D3D12 validation reported mesh errors");
        }
        std::cout << "{\"project\":\"TDU-Recompilation\",\"scope\":";
        Quoted(mesh.textured       ? "3dd-selected-material-texture-inspection"
               : mesh.scene_placed ? "3dd-selected-instance-resource-viewer"
                                   : "3dg-local-group-resource-viewer");
        std::cout
            << ",\"game_executed\":false,\"api\":\"D3D12\",\"process_bits\":"
            << sizeof(void*) * 8 << ",\"adapter\":";
        Quoted(native.adapterName());
        std::cout << ",\"group_name_bytes\":";
        if (options.scene_member)
            std::cout << "null";
        else
            Quoted(options.group);
        uint64_t drawn_triangles = 0;
        for (const auto& draw : mesh.draws)
            drawn_triangles += draw.index_count / 3;
        std::cout << ",\"member_index\":" << *options.member
                  << ",\"primitives\":" << mesh.primitives
                  << ",\"unique_groups\":" << mesh.unique_groups
                  << ",\"draw_count\":" << mesh.draws.size()
                  << ",\"filtered_primitive_draws\":"
                  << mesh.filtered_primitive_draws
                  << ",\"vertices\":" << mesh.vertices.size()
                  << ",\"triangles\":" << mesh.indices.size() / 3
                  << ",\"drawn_triangles\":" << drawn_triangles
                  << ",\"target_size\":" << options.size
                  << ",\"coverage_pixels\":" << covered
                  << ",\"geometry_coverage_pixels\":" << geometry_covered
                  << ",\"camera_yaw_degrees\":" << options.yaw
                  << ",\"camera_pitch_degrees\":" << options.pitch
                  << ",\"camera_fov_y_degrees\":45,\"source_axes_preserved\":"
                     "true,\"3dd_placement_applied\":"
                  << (mesh.scene_placed ? "true" : "false")
                  << ",\"scene_member_index\":";
        if (options.scene_member)
            std::cout << *options.scene_member;
        else
            std::cout << "null";
        std::cout << ",\"selected_node_indices\":[";
        for (std::size_t i = 0; i < options.nodes.size(); ++i) {
            if (i)
                std::cout << ',';
            std::cout << options.nodes[i].node_index;
        }
        std::cout << "],\"selected_object_entry_indices\":[";
        for (std::size_t i = 0; i < options.nodes.size(); ++i) {
            if (i)
                std::cout << ',';
            std::cout << options.nodes[i].object_entry_index.value_or(0);
        }
        std::cout << "],\"material_textures_applied\":"
                  << (mesh.textured ? "true" : "false")
                  << ",\"original_shaders_reconstructed\":false,\"inspection_"
                     "lighting\":"
                  << (mesh.textured ? "false" : "true")
                  << ",\"material_member_index\":";
        if (options.material_member)
            std::cout << *options.material_member;
        else
            std::cout << "null";
        std::cout << ",\"selected_texture_stage\":";
        if (options.texture_stage)
            std::cout << *options.texture_stage;
        else
            std::cout << "null";
        std::cout << ",\"texture_member_indices\":[";
        for (std::size_t i = 0; i < options.texture_members.size(); ++i) {
            if (i)
                std::cout << ',';
            std::cout << options.texture_members[i];
        }
        std::cout << "],\"material_draws\":[";
        const auto key_hex = [](const std::array<std::byte, 8>& key) {
            std::cout << '"';
            for (const auto byte : key)
                std::cout << std::hex << std::setw(2) << std::setfill('0')
                          << std::to_integer<unsigned>(byte);
            std::cout << std::dec << std::setfill(' ') << '"';
        };
        for (std::size_t i = 0; i < mesh.material_draws.size(); ++i) {
            if (i)
                std::cout << ',';
            const auto& binding = mesh.material_draws[i];
            std::cout << "{\"node\":" << binding.node_index
                      << ",\"entry\":" << binding.object_entry_index
                      << ",\"primitive\":" << binding.primitive_index
                      << ",\"material_index\":" << binding.material_index
                      << ",\"texture_index\":" << binding.texture_index
                      << ",\"material_key_hex\":";
            key_hex(binding.material_key);
            std::cout << ",\"texture_key_hex\":";
            key_hex(binding.texture_key);
            std::cout << ",\"sampler_indices\":[";
            for (std::size_t j = 0; j < binding.sampler_indices.size(); ++j) {
                if (j)
                    std::cout << ',';
                std::cout << unsigned(binding.sampler_indices[j]);
            }
            std::cout << "]}";
        }
        std::cout
            << "],\"inferred_material\":false,\"rendered_rgba_fnv1a64\":\""
            << std::hex << std::setw(16) << std::setfill('0')
            << Fingerprint(image.pixels) << std::dec << std::setfill(' ')
            << "\",\"present_frames\":" << frames
            << ",\"software\":" << (options.warp ? "true" : "false")
            << ",\"debug_layer\":" << (options.debug ? "true" : "false")
            << ",\"validation_errors\":" << (options.debug ? "0" : "null")
            << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "TDU-Recompilation 3DG group viewer: " << error.what()
                  << '\n';
        return 1;
    }
}
