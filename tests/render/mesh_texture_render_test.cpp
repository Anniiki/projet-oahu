// Source-owned D3D12 fixture. No game assets, shaders or commercial process.
#include "tdur/render/d3d12/mesh_viewer.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

using namespace tdur::render::d3d12;

namespace {
using RGBA = std::array<unsigned, 4>;
constexpr RGBA Red{255, 0, 0, 255};
constexpr RGBA Green{0, 255, 0, 255};
constexpr RGBA Blue{0, 0, 255, 255};
constexpr RGBA Yellow{255, 255, 0, 255};
constexpr RGBA White{255, 255, 255, 255};
constexpr RGBA Cyan{0, 255, 255, 255};
constexpr std::array<RGBA, 4> Palette{Red, Green, Blue, Yellow};
constexpr std::array<RGBA, 4> OtherPalette{Blue, Cyan, White, Red};
constexpr std::array<uint32_t, 6> Indices{0, 1, 2, 2, 1, 3};
uint64_t checks = 0;
uint32_t renders = 0;

void Require(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<std::byte> Pattern(uint32_t width, uint32_t height,
                               const std::array<RGBA, 4>& palette = Palette) {
    std::vector<std::byte> result(size_t(width) * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const auto& color = palette[(y % 2) * 2 + x % 2];
            for (size_t c = 0; c < 4; ++c) {
                result[(size_t(y) * width + x) * 4 + c] = std::byte(color[c]);
            }
        }
    }
    return result;
}

std::vector<std::byte> Solid(uint32_t width, uint32_t height, RGBA color) {
    return Pattern(width, height, {color, color, color, color});
}

std::array<MeshVertex, 4> Quad(float u0 = 0, float v0 = 0, float u1 = 1,
                               float v1 = 1) {
    return {{{{-1, 1, 0.5f}, {1, 1, 1, 1}, {u0, v0}},
             {{1, 1, 0.5f}, {1, 1, 1, 1}, {u1, v0}},
             {{-1, -1, 0.5f}, {1, 1, 1, 1}, {u0, v1}},
             {{1, -1, 0.5f}, {1, 1, 1, 1}, {u1, v1}}}};
}

MeshDraw Textured(uint32_t index = 0) {
    MeshDraw draw{0, 6};
    draw.texture_index = index;
    draw.sampler.min_filter = MeshTextureFilter::point;
    draw.sampler.mag_filter = MeshTextureFilter::point;
    draw.sampler.mip_filter = MeshMipFilter::none;
    return draw;
}

MeshReadback Render(MeshViewer& viewer, std::span<const MeshDraw> draws,
                    uint32_t width = 64, uint32_t height = 64) {
    auto image = viewer.render_readback(width, height, draws);
    ++renders;
    return image;
}

MeshReadback Render(MeshViewer& viewer, const MeshDraw& draw,
                    uint32_t width = 64, uint32_t height = 64) {
    return Render(viewer, std::span(&draw, 1), width, height);
}

RGBA Pixel(const MeshReadback& image, uint32_t x, uint32_t y) {
    Require(x < image.width && y < image.height, "Fixture pixel outside image");
    RGBA value{};
    for (size_t c = 0; c < value.size(); ++c) {
        value[c] = std::to_integer<unsigned>(
            image.pixels[(size_t(y) * image.width + x) * 4 + c]);
    }
    return value;
}

void Expect(const MeshReadback& image, uint32_t x, uint32_t y, RGBA expected,
            const char* message, unsigned tolerance = 0) {
    const auto value = Pixel(image, x, y);
    for (size_t c = 0; c < value.size(); ++c) {
        const auto delta = value[c] > expected[c] ? value[c] - expected[c]
                                                  : expected[c] - value[c];
        if (delta > tolerance) {
            std::cerr << message << " at " << x << ',' << y
                      << " actual=" << value[0] << ',' << value[1] << ','
                      << value[2] << ',' << value[3]
                      << " expected=" << expected[0] << ',' << expected[1]
                      << ',' << expected[2] << ',' << expected[3] << '\n';
        }
        Require(delta <= tolerance, message);
    }
}

template <class Operation>
void Refuses(NativeDevice& native, Operation operation) {
    const bool before = native.recording();
    bool refused = false;
    try {
        operation();
    } catch (const std::logic_error&) {
        refused = true;
    }
    Require(refused, "Invalid texture input was accepted");
    Require(native.recording() == before,
            "Rejected input changed command recording state");
}
} // namespace

int main(int argc, char** argv) {
    try {
        bool warp = false;
        bool debug = false;
        for (int i = 1; i < argc; ++i) {
            const std::string_view argument(argv[i]);
            if (argument == "--warp") {
                warp = true;
            } else if (argument == "--debug") {
                debug = true;
            } else {
                throw std::invalid_argument(
                    "Unknown textured mesh fixture argument");
            }
        }
        NativeDevice native;
        native.initialize(warp, debug);
        MeshViewer viewer(native);
        auto quad = Quad();
        viewer.upload(quad, Indices);
        auto pixels = Pattern(2, 2);
        std::array<MeshTextureLevel, 1> level{{{2, 2, pixels}}};
        std::array<MeshTexture, 1> texture{{{level}}};
        viewer.upload_textures(texture);
        auto draw = Textured();
        const auto asymmetry = Render(viewer, draw);
        for (uint32_t y = 0; y < 64; ++y) {
            for (uint32_t x = 0; x < 64; ++x) {
                Expect(asymmetry, x, y, Palette[(y >= 32) * 2 + (x >= 32)],
                       "Point texture lost UV orientation or channel order");
            }
        }
        std::fill(pixels.begin(), pixels.end(), std::byte{0});
        Require(Render(viewer, draw).pixels == asymmetry.pixels,
                "Texture source spans were retained instead of uploaded");
        pixels = Pattern(2, 2);
        level[0].pixels = pixels;

        // Two reused geometry instances, distinct textures AND distinct
        // addressing modes in one command list. Each wrong descriptor changes
        // a different expected pixel, even when the other binding is correct.
        auto otherPixels = Pattern(2, 2, OtherPalette);
        std::array<MeshTextureLevel, 1> otherLevel{{{2, 2, otherPixels}}};
        std::array<MeshTexture, 2> pair{{{level}, {otherLevel}}};
        viewer.upload_textures(pair);
        viewer.upload(Quad(1.25f, 0.25f, 1.25f, 0.25f), Indices);
        std::array<MeshDraw, 2> placed{Textured(0), Textured(1)};
        placed[0].transform[0] = placed[1].transform[0] = 0.5f;
        placed[0].transform[12] = -0.5f;
        placed[1].transform[12] = 0.5f;
        placed[1].sampler.address_u = MeshAddressMode::clamp;
        auto image = Render(viewer, placed);
        Expect(image, 16, 32, Red, "First texture/sampler descriptor leaked");
        Expect(image, 48, 32, Cyan, "Second texture/sampler descriptor leaked");
        std::swap(placed[0], placed[1]);
        Require(Render(viewer, placed).pixels == image.pixels,
                "Descriptor selection depends on batch order");

        std::array<MeshDraw, 2> layered{Textured(0), Textured(1)};
        layered[0].transform[14] = -0.3f;
        layered[1].transform[14] = 0.3f;
        layered[1].sampler.address_u = MeshAddressMode::clamp;
        image = Render(viewer, layered);
        Expect(image, 32, 32, Red, "Textured draws lost shared depth");
        std::swap(layered[0], layered[1]);
        Require(Render(viewer, layered).pixels == image.pixels,
                "Textured depth depends on draw order");
        layered[1].texture_index.reset();
        Expect(Render(viewer, layered), 32, 32, White,
               "Untextured foreground did not share depth with texture");

        // Descriptor collection replacement, then refusal must preserve it.
        pair = {{{otherLevel}, {level}}};
        viewer.upload_textures(pair);
        auto replacement = Textured();
        Expect(Render(viewer, replacement), 32, 32, Blue,
               "Replacing texture collection kept stale SRV");
        viewer.upload_textures(texture);
        const auto stable = Render(viewer, draw).pixels;
        auto rejectUpload = [&](std::span<const MeshTexture> invalid) {
            Refuses(native, [&] { viewer.upload_textures(invalid); });
            Require(Render(viewer, draw).pixels == stable,
                    "Rejected texture upload replaced previous collection");
        };
        std::array<MeshTexture, 1> missing{};
        rejectUpload(missing);
        auto badLevel = level;
        auto badTexture = texture;
        badTexture[0].levels = badLevel;
        badLevel[0].width = 0;
        rejectUpload(badTexture);
        badLevel[0] = level[0];
        badLevel[0].width = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION + 1;
        rejectUpload(badTexture);
        badLevel[0] = level[0];
        badLevel[0].pixels = std::span(pixels).first(15);
        rejectUpload(badTexture);
        std::vector<MeshTexture> tooMany(MeshViewer::MaxTextures + 1,
                                         texture[0]);
        rejectUpload(tooMany);
        std::array<MeshTextureLevel, MeshViewer::MaxMipLevelsPerTexture + 1>
            excessive{};
        std::array<MeshTexture, 1> excessiveTexture{{{excessive}}};
        rejectUpload(excessiveTexture);
        std::array<MeshTextureLevel, 2> wrongChain{level[0], level[0]};
        std::array<MeshTexture, 1> chainTexture{{{wrongChain}}};
        rejectUpload(chainTexture);
        auto single = Solid(1, 1, Red);
        wrongChain = {{{1, 1, single}, {1, 1, single}}};
        rejectUpload(chainTexture);
        std::array<std::vector<std::byte>, 5> mipStorage;
        std::array<MeshTextureLevel, 5> manyLevels;
        for (uint32_t i = 0; i < manyLevels.size(); ++i) {
            const auto size = 16U >> i;
            mipStorage[i] = Solid(size, size, Red);
            manyLevels[i] = {size, size, mipStorage[i]};
        }
        std::vector<MeshTexture> tooManyMips(MeshViewer::MaxTextures,
                                             MeshTexture{manyLevels});
        rejectUpload(tooManyMips);
        auto skinnyPixels = Solid(1, 8192, Red);
        std::array<MeshTextureLevel, 1> skinny{{{1, 8192, skinnyPixels}}};
        std::vector<MeshTexture> paddingBudget(129, MeshTexture{skinny});
        rejectUpload(paddingBudget);
        auto rawBudgetPixels = Solid(1024, 1024, Red);
        std::array<MeshTextureLevel, 1> rawBudgetLevel{
            {{1024, 1024, rawBudgetPixels}}};
        std::vector<MeshTexture> rawBudget(65, MeshTexture{rawBudgetLevel});
        rejectUpload(rawBudget);

        auto rejectDraw = [&](MeshDraw invalid) {
            const std::array<MeshDraw, 2> batch{draw, invalid};
            Refuses(native, [&] { (void)Render(viewer, batch); });
            Require(Render(viewer, draw).pixels == stable,
                    "Rejected sampler/texture index modified renderer");
        };
        auto invalid = draw;
        invalid.texture_index = std::numeric_limits<uint32_t>::max();
        rejectDraw(invalid);
        invalid = draw;
        invalid.sampler.min_filter = static_cast<MeshTextureFilter>(99);
        rejectDraw(invalid);
        invalid = draw;
        invalid.sampler.mag_filter = static_cast<MeshTextureFilter>(99);
        rejectDraw(invalid);
        invalid = draw;
        invalid.sampler.mip_filter = static_cast<MeshMipFilter>(99);
        rejectDraw(invalid);
        invalid = draw;
        invalid.sampler.address_u = static_cast<MeshAddressMode>(99);
        rejectDraw(invalid);
        invalid = draw;
        invalid.sampler.address_v = static_cast<MeshAddressMode>(99);
        rejectDraw(invalid);
        for (const float value :
             {-0.1f, 1.1f, std::numeric_limits<float>::quiet_NaN(),
              std::numeric_limits<float>::infinity()}) {
            invalid = draw;
            invalid.sampler.border_rgba[2] = value;
            rejectDraw(invalid);
        }
        std::vector<MeshDraw> samplerBudget(MeshViewer::MaxSamplers + 1, draw);
        for (size_t i = 0; i < samplerBudget.size(); ++i) {
            samplerBudget[i].sampler.border_rgba[0] = float(i) / 256;
        }
        Refuses(native, [&] { (void)Render(viewer, samplerBudget, 8, 8); });
        Require(Render(viewer, draw).pixels == stable,
                "Sampler budget refusal modified texture collection");
        (void)Render(viewer,
                     std::span(samplerBudget).first(MeshViewer::MaxSamplers), 8,
                     8);
        quad[0].texcoord[0] = std::numeric_limits<float>::quiet_NaN();
        Refuses(native, [&] { viewer.upload(quad, Indices); });
        Require(Render(viewer, draw).pixels == stable,
                "Nonfinite UV refusal modified geometry");
        std::array<MeshTextureLevel, 1> limitLevel{{{1, 1, single}}};
        std::vector<MeshTexture> maximumTextures(MeshViewer::MaxTextures,
                                                 MeshTexture{limitLevel});
        viewer.upload_textures(maximumTextures);
        Expect(Render(viewer, Textured(MeshViewer::MaxTextures - 1), 8, 8), 4,
               4, Red, "Highest allowed texture descriptor not usable");
        viewer.upload_textures(texture);

        // Point-address tests use explicit expected texel indices, independent
        // of the production enum conversion and descriptor-building functions.
        struct AddressCase {
            float coordinate;
            std::array<int, 4> texel;
        };
        const std::array<AddressCase, 6> cases{{{-1.25f, {1, 0, 1, -1}},
                                                {-0.25f, {1, 0, 0, -1}},
                                                {0.25f, {0, 0, 0, 0}},
                                                {0.75f, {1, 1, 1, 1}},
                                                {1.25f, {0, 1, 1, -1}},
                                                {2.25f, {0, 1, 0, -1}}}};
        constexpr std::array<MeshAddressMode, 4> modes{
            MeshAddressMode::wrap, MeshAddressMode::clamp,
            MeshAddressMode::mirror, MeshAddressMode::border};
        constexpr RGBA Border{17, 34, 51, 68};
        for (bool vertical : {false, true}) {
            for (const auto& scenario : cases) {
                const float u = vertical ? 0.25f : scenario.coordinate;
                const float v = vertical ? scenario.coordinate : 0.25f;
                viewer.upload(Quad(u, v, u, v), Indices);
                for (size_t mode = 0; mode < modes.size(); ++mode) {
                    auto addressed = draw;
                    addressed.sampler.address_u = modes[mode];
                    addressed.sampler.address_v = modes[mode];
                    for (size_t c = 0; c < 4; ++c) {
                        addressed.sampler.border_rgba[c] =
                            float(Border[c]) / 255;
                    }
                    const auto texel = scenario.texel[mode];
                    const auto expected =
                        texel < 0 ? Border
                                  : Palette[size_t(texel) * (vertical ? 2 : 1)];
                    Expect(Render(viewer, addressed, 8, 8), 4, 4, expected,
                           "Point address mode or RGBA border differs");
                }
            }
        }

        viewer.upload(Quad(0.5f, 0.5f, 0.5f, 0.5f), Indices);
        auto filtered = draw;
        filtered.sampler.mag_filter = MeshTextureFilter::linear;
        Expect(Render(viewer, filtered, 8, 8), 4, 4, {128, 128, 64, 255},
               "MAG linear did not blend four magnified texels");
        filtered.sampler.mag_filter = MeshTextureFilter::point;
        filtered.sampler.min_filter = MeshTextureFilter::linear;
        Expect(Render(viewer, filtered, 8, 8), 4, 4, Yellow,
               "MAG point incorrectly used independent MIN linear filter");

        std::array<std::vector<std::byte>, 4> patternedMips;
        std::array<MeshTextureLevel, 4> mipLevels;
        for (uint32_t i = 0; i < mipLevels.size(); ++i) {
            const auto size = 8U >> i;
            patternedMips[i] = Pattern(size, size);
            mipLevels[i] = {size, size, patternedMips[i]};
        }
        std::array<MeshTexture, 1> mipTexture{{{mipLevels}}};
        viewer.upload_textures(mipTexture);
        viewer.upload(Quad(0.125f, 0.125f, 1.125f, 1.125f), Indices);
        filtered = draw;
        filtered.sampler.mip_filter = MeshMipFilter::point;
        filtered.sampler.min_filter = MeshTextureFilter::linear;
        Expect(Render(viewer, filtered, 4, 4), 0, 0, {128, 128, 64, 255},
               "MIN linear did not blend minified mip1 texels");
        filtered.sampler.min_filter = MeshTextureFilter::point;
        filtered.sampler.mag_filter = MeshTextureFilter::linear;
        Expect(Render(viewer, filtered, 4, 4), 0, 0, Yellow,
               "MIN point incorrectly used independent MAG linear filter");

        for (uint32_t i = 0; i < mipLevels.size(); ++i) {
            const auto size = 8U >> i;
            patternedMips[i] = Solid(size, size, Palette[i]);
            mipLevels[i].pixels = patternedMips[i];
        }
        viewer.upload_textures(mipTexture);
        const float extent = std::sqrt(2.0f) / 2;
        viewer.upload(Quad(0, 0, extent, extent), Indices);
        filtered = draw;
        Expect(Render(viewer, filtered, 4, 4), 2, 2, Red,
               "MIP none did not stay on level0 under minification");
        filtered.sampler.mip_filter = MeshMipFilter::linear;
        const auto mipBlend = Render(viewer, filtered, 4, 4);
        Expect(mipBlend, 2, 2, {128, 128, 0, 255},
               "Fractional LOD did not interpolate uploaded mip0/mip1", 1);
        viewer.upload(Quad(0, 0, 8, 8), Indices);
        filtered.sampler.mip_filter = MeshMipFilter::point;
        Expect(Render(viewer, filtered, 8, 8), 4, 4, Yellow,
               "Last uploaded 1x1 mip was not selectable");

        // Odd/non-square source rows exercise GetCopyableFootprints padding.
        auto oddPixels = Pattern(5, 3);
        std::array<MeshTextureLevel, 1> oddLevel{{{5, 3, oddPixels}}};
        std::array<MeshTexture, 1> oddTexture{{{oddLevel}}};
        viewer.upload_textures(oddTexture);
        viewer.upload(Quad(), Indices);
        const auto oddImage = Render(viewer, draw, 5, 3);
        for (uint32_t y = 0; y < 3; ++y) {
            for (uint32_t x = 0; x < 5; ++x) {
                Expect(oddImage, x, y, Palette[(y % 2) * 2 + x % 2],
                       "Odd width texture upload lost row alignment");
            }
        }
        auto partialPixels = Solid(2, 1, Cyan);
        std::array<MeshTextureLevel, 2> partialLevels{
            oddLevel[0], MeshTextureLevel{2, 1, partialPixels}};
        std::array<MeshTexture, 1> partialTexture{{{partialLevels}}};
        viewer.upload_textures(partialTexture);
        auto partialDraw = draw;
        partialDraw.sampler.mip_filter = MeshMipFilter::point;
        Expect(
            Render(viewer, partialDraw, 1, 1), 0, 0, Cyan,
            "Partial odd-size mip chain did not clamp to its last SRV level");
        viewer.upload_textures(oddTexture);
        auto tint = Quad();
        for (auto& vertex : tint) {
            vertex.color = {0, 1, 0, 1};
        }
        viewer.upload(tint, Indices);
        Expect(Render(viewer, draw, 5, 3), 1, 0, Green,
               "Textured pixel did not multiply vertex color");
        viewer.upload(Quad(), Indices);
        auto alphaPixels = Solid(1, 1, {17, 34, 51, 68});
        std::array<MeshTextureLevel, 1> alphaLevel{{{1, 1, alphaPixels}}};
        std::array<MeshTexture, 1> alphaTexture{{{alphaLevel}}};
        viewer.upload_textures(alphaTexture);
        Expect(Render(viewer, draw, 8, 8), 4, 4, {17, 34, 51, 68},
               "Texture alpha was dropped or premultiplied");

        native.beginFrame();
        Refuses(native, [&] { viewer.upload_textures(texture); });
        Refuses(native, [&] { (void)Render(viewer, draw); });
        native.wait(native.submit());
        viewer.upload_textures({});
        Refuses(native, [&] { (void)Render(viewer, draw); });
        const MeshDraw colored{0, 6};
        Expect(Render(viewer, colored, 8, 8), 4, 4, White,
               "Clearing textures broke original untextured draw");
        Require(native.validationErrorCount() == 0, "D3D12 validation error");
        std::cout
            << "{\"project\":\"TDU-Recompilation\",\"scope\":\"authored-"
               "textured-mesh-fixture\",\"api\":\"D3D12\","
               "\"game_executed\":false,\"adapter\":\""
            << native.adapterName()
            << "\",\"software\":" << (warp ? "true" : "false")
            << ",\"debug_layer\":" << (debug ? "true" : "false")
            << ",\"checks\":" << checks << ",\"readbacks\":" << renders
            << ",\"uv_asymmetry\":true,\"two_textures_two_samplers\":true,"
               "\"shared_depth\":true,\"mip_chain\":true,"
               "\"refusal_immutability\":true,\"validation_errors\":"
            << (debug ? "0" : "null") << "}\n";
    } catch (const std::exception& error) {
        std::cerr << "Source-owned textured mesh fixture: " << error.what()
                  << '\n';
        return 1;
    }
}
