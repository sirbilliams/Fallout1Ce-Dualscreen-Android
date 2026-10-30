#include "mui/mui_draw.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <tuple>

#include <string.h>

#include "game/art.h"
#include "mui/mui_charset.h"
#include "mui/mui_gameskin.h"
#include "mui/mui_icons.h"
#include "mui/mui_skin.h"
#include "plib/color/color.h"
#include "plib/gnw/svga.h"


namespace fallout {

namespace {

    SDL_Renderer* gRenderer = nullptr;

    std::vector<SDL_Rect> gClipStack;

    struct ArtTexture {
        SDL_Texture* texture = nullptr;
        int width = 0;
        int height = 0;
        unsigned int paletteVersion = 0;
        // The art isn't there (not looked up again).
        bool missing = false;
    };

    // By fid and frame.
    std::map<std::pair<int, int>, ArtTexture> gArtTextures;

    // Painted pictures by fid.
    // Named FRMs all have the same empty FID. Own the path in the key and
    // include its object type (relative interface path vs full asset path).
    std::map<std::tuple<int, int, std::string>, ArtTexture> gPictureTextures;

    // Line drawings (skilldex pictures) by fid.
    std::map<int, ArtTexture> gLineArtTextures;

    struct IndexedTexture {
        ArtTexture base;
        unsigned int version = 0;
    };
    std::map<std::string, IndexedTexture> gIndexedTextures;
    unsigned char gPalette[768];
    unsigned int gPaletteVersion = 0;
    unsigned int gPaletteCheckFrame = 0;
    unsigned int gFrame = 0;

    std::vector<SDL_Vertex> gVertices;

    SDL_Vertex vertex(float x, float y, MuiColor color, float u = 0.0f, float v = 0.0f)
    {
        SDL_Vertex result;
        result.position.x = x;
        result.position.y = y;
        result.color = { color.r, color.g, color.b, color.a };
        result.tex_coord.x = u;
        result.tex_coord.y = v;
        return result;
    }

    // Outline of a rounded rect, clockwise.
    int roundRectSegments(float radius)
    {
        return std::clamp(static_cast<int>(radius / 2.0f), 3, 12);
    }

    void roundRectOutline(const MuiRect& rect, float radius, std::vector<SDL_FPoint>& points, int segments)
    {
        radius = std::max(std::min(radius, std::min(rect.w, rect.h) / 2.0f), 0.0f);

        struct Corner {
            float cx;
            float cy;
            float start;
        };

        const float pi = 3.14159265f;
        Corner corners[4] = {
            { rect.x + rect.w - radius, rect.y + radius, -pi / 2.0f },
            { rect.x + rect.w - radius, rect.y + rect.h - radius, 0.0f },
            { rect.x + radius, rect.y + rect.h - radius, pi / 2.0f },
            { rect.x + radius, rect.y + radius, pi },
        };

        points.clear();
        for (const Corner& corner : corners) {
            for (int step = 0; step <= segments; step++) {
                float angle = corner.start + (pi / 2.0f) * step / segments;
                points.push_back({ corner.cx + std::cos(angle) * radius, corner.cy + std::sin(angle) * radius });
            }
        }
    }

    void fillPolygon(const std::vector<SDL_FPoint>& points, float centerX, float centerY, MuiColor color)
    {
        gVertices.clear();
        size_t count = points.size();
        for (size_t index = 0; index < count; index++) {
            const SDL_FPoint& a = points[index];
            const SDL_FPoint& b = points[(index + 1) % count];
            gVertices.push_back(vertex(centerX, centerY, color));
            gVertices.push_back(vertex(a.x, a.y, color));
            gVertices.push_back(vertex(b.x, b.y, color));
        }
        SDL_RenderGeometry(gRenderer, nullptr, gVertices.data(), static_cast<int>(gVertices.size()), nullptr, 0);
    }

    void strokePolygons(const std::vector<SDL_FPoint>& outer, const std::vector<SDL_FPoint>& inner, MuiColor color)
    {
        gVertices.clear();
        size_t count = std::min(outer.size(), inner.size());
        for (size_t index = 0; index < count; index++) {
            size_t next = (index + 1) % count;
            gVertices.push_back(vertex(outer[index].x, outer[index].y, color));
            gVertices.push_back(vertex(outer[next].x, outer[next].y, color));
            gVertices.push_back(vertex(inner[index].x, inner[index].y, color));
            gVertices.push_back(vertex(inner[index].x, inner[index].y, color));
            gVertices.push_back(vertex(outer[next].x, outer[next].y, color));
            gVertices.push_back(vertex(inner[next].x, inner[next].y, color));
        }
        SDL_RenderGeometry(gRenderer, nullptr, gVertices.data(), static_cast<int>(gVertices.size()), nullptr, 0);
    }

    void circleOutline(float centerX, float centerY, float radius, std::vector<SDL_FPoint>& points)
    {
        int segments = std::clamp(static_cast<int>(radius), 16, 96);
        points.clear();
        for (int step = 0; step < segments; step++) {
            float angle = 2.0f * 3.14159265f * step / segments;
            points.push_back({ centerX + std::cos(angle) * radius, centerY + std::sin(angle) * radius });
        }
    }

} // namespace

void muiDrawBegin(SDL_Renderer* renderer)
{
    gRenderer = renderer;
    gClipStack.clear();
    gFrame++;
    SDL_SetRenderDrawBlendMode(gRenderer, SDL_BLENDMODE_BLEND);
}

void muiDrawEnd()
{
    if (gRenderer != nullptr) {
        SDL_RenderSetClipRect(gRenderer, nullptr);
    }
    gClipStack.clear();
}

static unsigned int gDrawGeneration = 1;

unsigned int muiDrawGeneration()
{
    return gDrawGeneration;
}

SDL_BlendMode muiPremultipliedBlendMode()
{
    return SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
        SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
}

void muiDrawReset()
{
    gDrawGeneration++;

    for (auto& entry : gIndexedTextures) {
        if (entry.second.base.texture != nullptr) {
            SDL_DestroyTexture(entry.second.base.texture);
        }
    }
    gIndexedTextures.clear();

    for (auto& entry : gArtTextures) {
        if (entry.second.texture != nullptr) {
            SDL_DestroyTexture(entry.second.texture);
        }
    }
    gArtTextures.clear();

    for (auto& entry : gPictureTextures) {
        if (entry.second.texture != nullptr) {
            SDL_DestroyTexture(entry.second.texture);
        }
    }
    gPictureTextures.clear();

    for (auto& entry : gLineArtTextures) {
        if (entry.second.texture != nullptr) {
            SDL_DestroyTexture(entry.second.texture);
        }
    }
    gLineArtTextures.clear();

    muiIconsReset();
    muiGameSkinReset();
    gRenderer = nullptr;
}

SDL_Renderer* muiDrawGetRenderer()
{
    return gRenderer;
}

void muiFillRect(const MuiRect& rect, MuiColor color)
{
    SDL_FRect frect = { rect.x, rect.y, rect.w, rect.h };
    SDL_SetRenderDrawColor(gRenderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRectF(gRenderer, &frect);
}

void muiFillRoundRect(const MuiRect& rect, float radius, MuiColor color)
{
    if (muiSkinFallout() && radius >= 1.0f) {
        // The game's art has square corners with a notch, not round ones.
        float p = 2.0f;
        if (rect.w > p * 6.0f && rect.h > p * 6.0f) {
            muiFillRect({ rect.x + p * 2.0f, rect.y, rect.w - p * 4.0f, p }, color);
            muiFillRect({ rect.x + p, rect.y + p, rect.w - p * 2.0f, p }, color);
            muiFillRect({ rect.x, rect.y + p * 2.0f, rect.w, rect.h - p * 4.0f }, color);
            muiFillRect({ rect.x + p, rect.bottom() - p * 2.0f, rect.w - p * 2.0f, p }, color);
            muiFillRect({ rect.x + p * 2.0f, rect.bottom() - p, rect.w - p * 4.0f, p }, color);
            return;
        }
        muiFillRect(rect, color);
        return;
    }
    if (radius < 1.0f) {
        muiFillRect(rect, color);
        return;
    }

    std::vector<SDL_FPoint> points;
    roundRectOutline(rect, radius, points, roundRectSegments(radius));
    fillPolygon(points, rect.centerX(), rect.centerY(), color);
}

void muiStrokeRoundRect(const MuiRect& rect, float radius, float thickness, MuiColor color)
{
    if (muiSkinFallout() && radius >= 1.0f) {
        float p = 2.0f;
        float t = std::max(thickness, 1.0f);
        if (rect.w > p * 6.0f && rect.h > p * 6.0f) {
            muiFillRect({ rect.x + p * 2.0f, rect.y, rect.w - p * 4.0f, t }, color);
            muiFillRect({ rect.x + p * 2.0f, rect.bottom() - t, rect.w - p * 4.0f, t }, color);
            muiFillRect({ rect.x, rect.y + p * 2.0f, t, rect.h - p * 4.0f }, color);
            muiFillRect({ rect.right() - t, rect.y + p * 2.0f, t, rect.h - p * 4.0f }, color);
            muiFillRect({ rect.x + p, rect.y + p, t, t }, color);
            muiFillRect({ rect.right() - p - t, rect.y + p, t, t }, color);
            muiFillRect({ rect.x + p, rect.bottom() - p - t, t, t }, color);
            muiFillRect({ rect.right() - p - t, rect.bottom() - p - t, t, t }, color);
            return;
        }
        muiSkinBevel(rect, t, color, color, false);
        return;
    }
    // Same segment count, so outlines have the same point count.
    int segments = roundRectSegments(radius);
    std::vector<SDL_FPoint> outer;
    std::vector<SDL_FPoint> inner;
    roundRectOutline(rect, radius, outer, segments);
    roundRectOutline(rect.inset(thickness), std::max(radius - thickness, 0.0f), inner, segments);
    strokePolygons(outer, inner, color);
}

void muiFillCircle(float centerX, float centerY, float radius, MuiColor color)
{
    std::vector<SDL_FPoint> points;
    circleOutline(centerX, centerY, radius, points);
    fillPolygon(points, centerX, centerY, color);
}

void muiStrokeCircle(float centerX, float centerY, float radius, float thickness, MuiColor color)
{
    std::vector<SDL_FPoint> outer;
    std::vector<SDL_FPoint> inner;
    circleOutline(centerX, centerY, radius, outer);
    circleOutline(centerX, centerY, radius, inner);
    for (size_t index = 0; index < inner.size(); index++) {
        inner[index].x = centerX + (inner[index].x - centerX) * (radius - thickness) / radius;
        inner[index].y = centerY + (inner[index].y - centerY) * (radius - thickness) / radius;
    }
    strokePolygons(outer, inner, color);
}

void muiFillHorizontalGradient(const MuiRect& rect, MuiColor left, MuiColor right)
{
    muiFillGradient(rect, left, right, right, left);
}

void muiFillGradient(const MuiRect& rect, MuiColor topLeft, MuiColor topRight, MuiColor bottomRight, MuiColor bottomLeft)
{
    if (rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }

    gVertices.clear();
    gVertices.push_back(vertex(rect.x, rect.y, topLeft));
    gVertices.push_back(vertex(rect.right(), rect.y, topRight));
    gVertices.push_back(vertex(rect.right(), rect.bottom(), bottomRight));
    gVertices.push_back(vertex(rect.x, rect.y, topLeft));
    gVertices.push_back(vertex(rect.right(), rect.bottom(), bottomRight));
    gVertices.push_back(vertex(rect.x, rect.bottom(), bottomLeft));
    SDL_RenderGeometry(gRenderer, nullptr, gVertices.data(), static_cast<int>(gVertices.size()), nullptr, 0);
}

void muiDrawLine(float x1, float y1, float x2, float y2, float thickness, MuiColor color)
{
    float dx = x2 - x1;
    float dy = y2 - y1;
    float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.0f) {
        return;
    }

    // Quad around the segment.
    float nx = -dy / length * thickness / 2.0f;
    float ny = dx / length * thickness / 2.0f;
    gVertices.clear();
    gVertices.push_back(vertex(x1 + nx, y1 + ny, color));
    gVertices.push_back(vertex(x2 + nx, y2 + ny, color));
    gVertices.push_back(vertex(x2 - nx, y2 - ny, color));
    gVertices.push_back(vertex(x1 + nx, y1 + ny, color));
    gVertices.push_back(vertex(x2 - nx, y2 - ny, color));
    gVertices.push_back(vertex(x1 - nx, y1 - ny, color));
    SDL_RenderGeometry(gRenderer, nullptr, gVertices.data(), static_cast<int>(gVertices.size()), nullptr, 0);
}

void muiFillConvex(const std::vector<SDL_FPoint>& points, MuiColor color)
{
    if (points.size() < 3) {
        return;
    }

    float centerX = 0.0f;
    float centerY = 0.0f;
    for (const SDL_FPoint& point : points) {
        centerX += point.x;
        centerY += point.y;
    }
    fillPolygon(points, centerX / points.size(), centerY / points.size(), color);
}

void muiDrawPolyline(const std::vector<SDL_FPoint>& points, float thickness, MuiColor color, bool closed)
{
    size_t count = points.size();
    size_t segments = closed ? count : count - 1;
    for (size_t index = 0; count > 1 && index < segments; index++) {
        const SDL_FPoint& a = points[index];
        const SDL_FPoint& b = points[(index + 1) % count];
        muiDrawLine(a.x, a.y, b.x, b.y, thickness, color);
    }

    for (const SDL_FPoint& point : points) {
        muiFillCircle(point.x, point.y, thickness / 2.0f, color);
    }
}

void muiPushClip(const MuiRect& rect)
{
    SDL_Rect clip = {
        static_cast<int>(std::floor(rect.x)),
        static_cast<int>(std::floor(rect.y)),
        static_cast<int>(std::ceil(rect.w)),
        static_cast<int>(std::ceil(rect.h)),
    };

    if (!gClipStack.empty()) {
        SDL_Rect intersection;
        if (!SDL_IntersectRect(&clip, &gClipStack.back(), &intersection)) {
            intersection = { clip.x, clip.y, 0, 0 };
        }
        clip = intersection;
    }

    gClipStack.push_back(clip);
    SDL_RenderSetClipRect(gRenderer, &clip);
}

void muiPopClip()
{
    if (!gClipStack.empty()) {
        gClipStack.pop_back();
    }

    SDL_RenderSetClipRect(gRenderer, gClipStack.empty() ? nullptr : &gClipStack.back());
}

std::u32string muiDecodeGameText(const char* text)
{
    std::u32string result;
    if (text == nullptr) {
        return result;
    }

    for (const unsigned char* ch = reinterpret_cast<const unsigned char*>(text); *ch != '\0'; ch++) {
        result.push_back(muiCharsetDecode(*ch));
    }
    return result;
}

float muiFitTextSize(const std::u32string& text, float width, float size, float minSize)
{
    while (size > minSize && muiTextWidth(text, size) > width) {
        size -= 0.5f;
    }
    return size;
}

std::string muiEncodeGameText(const std::u32string& text)
{
    std::string result;
    for (char32_t codepoint : text) {
        // Not in the game's charset: left out.
        unsigned char ch = muiCharsetEncode(codepoint);
        if (ch != 0) {
            result.push_back(static_cast<char>(ch));
        }
    }
    return result;
}

std::u32string muiDecodeUtf8(const char* text)
{
    std::u32string result;
    if (text == nullptr) {
        return result;
    }

    const unsigned char* ch = reinterpret_cast<const unsigned char*>(text);
    while (*ch != '\0') {
        char32_t codepoint;
        int extra;
        if (*ch < 0x80) {
            codepoint = *ch;
            extra = 0;
        } else if ((*ch & 0xE0) == 0xC0) {
            codepoint = *ch & 0x1F;
            extra = 1;
        } else if ((*ch & 0xF0) == 0xE0) {
            codepoint = *ch & 0x0F;
            extra = 2;
        } else {
            codepoint = *ch & 0x07;
            extra = 3;
        }
        ch++;
        for (int index = 0; index < extra && *ch != '\0'; index++, ch++) {
            codepoint = (codepoint << 6) | (*ch & 0x3F);
        }
        result.push_back(codepoint);
    }
    return result;
}

float muiLineHeight(float size)
{
    return muiGameLineHeight(size);
}

bool muiFontHasGlyph(char32_t codepoint)
{
    return codepoint >= 0x20 && codepoint < 0x180;
}

float muiTextWidth(const std::u32string& text, float size)
{
    return muiGameTextWidth(text, size);
}

void muiDrawText(const std::u32string& text, float x, float y, float size, MuiColor color)
{
    muiGameDrawText(text, x, y, size, color);
}

void muiDrawTextAligned(const std::u32string& text, const MuiRect& rect, float size, MuiColor color, MuiAlign horizontal, MuiAlign vertical)
{
    float width = muiTextWidth(text, size);
    float height = muiLineHeight(size);

    float x = rect.x;
    if (horizontal == MuiAlign::Center) {
        x = rect.x + (rect.w - width) / 2.0f;
    } else if (horizontal == MuiAlign::End) {
        x = rect.right() - width;
    }

    float y = rect.y;
    if (vertical == MuiAlign::Center) {
        y = rect.y + (rect.h - height) / 2.0f;
    } else if (vertical == MuiAlign::End) {
        y = rect.bottom() - height;
    }

    muiDrawText(text, x, y, size, color);
}

// Palette changes (fades, other palettes), textures follow it.
static void checkPalette()
{
    if (gPaletteCheckFrame != gFrame) {
        gPaletteCheckFrame = gFrame;
        unsigned char* palette = getSystemPalette();
        if (memcmp(palette, gPalette, sizeof(gPalette)) != 0) {
            memcpy(gPalette, palette, sizeof(gPalette));
            gPaletteVersion++;
        }
    }
}

// Converts 8-bit image to texture [entry] (created or reused).
static bool uploadIndexed(ArtTexture& entry, const unsigned char* data, int width, int height)
{
    std::vector<Uint32> pixels(static_cast<size_t>(width) * height);
    for (size_t index = 0; index < pixels.size(); index++) {
        unsigned char color = data[index];
        if (color == 0) {
            pixels[index] = 0;
        } else {
            // Palette is 6 bits per channel.
            Uint32 r = gPalette[color * 3] << 2;
            Uint32 g = gPalette[color * 3 + 1] << 2;
            Uint32 b = gPalette[color * 3 + 2] << 2;
            pixels[index] = 0xFF000000 | (r << 16) | (g << 8) | b;
        }
    }

    if (entry.texture != nullptr && (entry.width != width || entry.height != height)) {
        SDL_DestroyTexture(entry.texture);
        entry.texture = nullptr;
    }

    if (entry.texture == nullptr) {
        entry.texture = SDL_CreateTexture(gRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, width, height);
        if (entry.texture == nullptr) {
            return false;
        }
        SDL_SetTextureBlendMode(entry.texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(entry.texture, SDL_ScaleModeNearest);
    }

    SDL_UpdateTexture(entry.texture, nullptr, pixels.data(), width * 4);
    entry.width = width;
    entry.height = height;
    entry.paletteVersion = gPaletteVersion;
    return true;
}

SDL_Texture* muiIndexedTexture(const std::string& key, const unsigned char* data, int width, int height, unsigned int version)
{
    if (gRenderer == nullptr || data == nullptr || width <= 0 || height <= 0) {
        return nullptr;
    }

    checkPalette();

    IndexedTexture& entry = gIndexedTextures[key];
    if (entry.base.texture != nullptr && entry.version == version && entry.base.paletteVersion == gPaletteVersion
        && entry.base.width == width && entry.base.height == height) {
        return entry.base.texture;
    }

    if (!uploadIndexed(entry.base, data, width, height)) {
        return nullptr;
    }
    entry.version = version;
    return entry.base.texture;
}

SDL_Texture* muiRgbaTexture(const std::string& key, const unsigned char* data, int width, int height, unsigned int version)
{
    if (gRenderer == nullptr || data == nullptr || width <= 0 || height <= 0) {
        return nullptr;
    }

    IndexedTexture& entry = gIndexedTextures["rgba." + key];
    if (entry.base.texture != nullptr && entry.version == version && entry.base.width == width && entry.base.height == height) {
        return entry.base.texture;
    }

    if (entry.base.texture != nullptr && (entry.base.width != width || entry.base.height != height)) {
        SDL_DestroyTexture(entry.base.texture);
        entry.base.texture = nullptr;
    }

    if (entry.base.texture == nullptr) {
        entry.base.texture = SDL_CreateTexture(gRenderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, width, height);
        if (entry.base.texture == nullptr) {
            return nullptr;
        }
        SDL_SetTextureBlendMode(entry.base.texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(entry.base.texture, SDL_ScaleModeNearest);
    }

    SDL_UpdateTexture(entry.base.texture, nullptr, data, width * 4);
    entry.base.width = width;
    entry.base.height = height;
    entry.version = version;
    return entry.base.texture;
}

SDL_Texture* muiArtTexture(int fid, int frame, int* width, int* height, int rotation)
{
    if (gRenderer == nullptr) {
        return nullptr;
    }

    checkPalette();

    auto key = std::make_pair(fid, frame * 8 + rotation);
    ArtTexture& entry = gArtTextures[key];
    if (entry.texture != nullptr && entry.paletteVersion == gPaletteVersion) {
        *width = entry.width;
        *height = entry.height;
        return entry.texture;
    }

    CacheEntry* handle;
    Art* art = art_ptr_lock(fid, &handle);
    if (art == nullptr) {
        return nullptr;
    }

    int artWidth = art_frame_width(art, frame, rotation);
    int artHeight = art_frame_length(art, frame, rotation);
    unsigned char* data = art_frame_data(art, frame, rotation);
    if (data == nullptr || artWidth <= 0 || artHeight <= 0) {
        art_ptr_unlock(handle);
        return nullptr;
    }

    bool uploaded = uploadIndexed(entry, data, artWidth, artHeight);
    art_ptr_unlock(handle);
    if (!uploaded) {
        return nullptr;
    }

    *width = artWidth;
    *height = artHeight;
    return entry.texture;
}

SDL_Texture* muiPictureTexture(int fid, int* width, int* height)
{
    *width = 0;
    *height = 0;
    return muiArtTexture(fid, 0, width, height, 0);
}

SDL_Texture* muiLineArtTexture(int fid, int* width, int* height)
{
    if (gRenderer == nullptr) {
        return nullptr;
    }

    ArtTexture& entry = gLineArtTextures[fid];
    if (entry.texture != nullptr) {
        *width = entry.width;
        *height = entry.height;
        return entry.texture;
    }

    CacheEntry* handle;
    Art* art = art_ptr_lock(fid, &handle);
    if (art == nullptr) {
        return nullptr;
    }

    int artWidth = art_frame_width(art, 0, 0);
    int artHeight = art_frame_length(art, 0, 0);
    unsigned char* data = art_frame_data(art, 0, 0);
    if (data == nullptr || artWidth <= 0 || artHeight <= 0) {
        art_ptr_unlock(handle);
        return nullptr;
    }

    // Ink is what is much darker than the paper (the game's color table, so
    // fades don't matter): opaque below the paper's darkest tones, fading
    // out towards them.
    constexpr float kInk = 27.0f;
    constexpr float kPaper = 72.0f;
    Uint32 colors[256];
    colors[0] = 0;
    for (int index = 1; index < 256; index++) {
        float r = cmap[index * 3] * 4.0f;
        float g = cmap[index * 3 + 1] * 4.0f;
        float b = cmap[index * 3 + 2] * 4.0f;
        float luminance = 0.299f * r + 0.587f * g + 0.114f * b;
        float alpha = std::clamp((kPaper - luminance) / (kPaper - kInk), 0.0f, 1.0f);
        colors[index] = (static_cast<Uint32>(alpha * 255.0f + 0.5f) << 24) | 0x00FFFFFF;
    }

    std::vector<Uint32> pixels(static_cast<size_t>(artWidth) * artHeight);
    for (size_t index = 0; index < pixels.size(); index++) {
        pixels[index] = colors[data[index]];
    }
    art_ptr_unlock(handle);

    entry.texture = SDL_CreateTexture(gRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, artWidth, artHeight);
    if (entry.texture == nullptr) {
        return nullptr;
    }
    SDL_SetTextureBlendMode(entry.texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(entry.texture, SDL_ScaleModeLinear);
    SDL_UpdateTexture(entry.texture, nullptr, pixels.data(), artWidth * 4);

    entry.width = artWidth;
    entry.height = artHeight;
    *width = artWidth;
    *height = artHeight;
    return entry.texture;
}

void muiDrawTexture(SDL_Texture* texture, const MuiRect& rect, MuiColor tint)
{
    if (texture == nullptr) {
        return;
    }

    SDL_SetTextureColorMod(texture, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(texture, tint.a);
    SDL_FRect dest = { rect.x, rect.y, rect.w, rect.h };
    SDL_RenderCopyF(gRenderer, texture, nullptr, &dest);
}

void muiDrawTexturePart(SDL_Texture* texture, const SDL_Rect& source, const MuiRect& rect, MuiColor tint)
{
    if (texture == nullptr || source.w <= 0 || source.h <= 0) {
        return;
    }

    SDL_SetTextureColorMod(texture, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(texture, tint.a);
    SDL_FRect dest = { rect.x, rect.y, rect.w, rect.h };
    SDL_RenderCopyF(gRenderer, texture, &source, &dest);
}

void muiDrawTextureCover(SDL_Texture* texture, const MuiRect& rect, MuiColor tint)
{
    if (texture == nullptr || rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }

    int width;
    int height;
    if (SDL_QueryTexture(texture, nullptr, nullptr, &width, &height) != 0 || width <= 0 || height <= 0) {
        return;
    }

    float sourceAspect = static_cast<float>(width) / height;
    float targetAspect = rect.w / rect.h;
    SDL_Rect source = { 0, 0, width, height };
    if (sourceAspect > targetAspect) {
        source.w = std::max(1, static_cast<int>(std::round(height * targetAspect)));
        source.x = (width - source.w) / 2;
    } else if (sourceAspect < targetAspect) {
        source.h = std::max(1, static_cast<int>(std::round(width / targetAspect)));
        source.y = (height - source.h) / 2;
    }

    SDL_SetTextureColorMod(texture, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(texture, tint.a);
    SDL_FRect dest = { rect.x, rect.y, rect.w, rect.h };
    SDL_RenderCopyF(gRenderer, texture, &source, &dest);
}

MuiRect muiFitRect(const MuiRect& rect, float width, float height)
{
    if (width <= 0.0f || height <= 0.0f) {
        return rect;
    }

    float scale = std::min(rect.w / width, rect.h / height);
    float w = width * scale;
    float h = height * scale;
    return { rect.x + (rect.w - w) / 2.0f, rect.y + (rect.h - h) / 2.0f, w, h };
}

std::vector<std::u32string> muiWrapText(const std::u32string& text, float width, float size)
{
    std::vector<std::u32string> lines;

    // Explicit line breaks first.
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(U'\n', start);
        std::u32string paragraph = text.substr(start, end == std::u32string::npos ? std::u32string::npos : end - start);

        std::u32string line;
        size_t position = 0;
        while (position < paragraph.size()) {
            size_t space = paragraph.find(U' ', position);
            std::u32string word = paragraph.substr(position, space == std::u32string::npos ? std::u32string::npos : space - position);
            position = space == std::u32string::npos ? paragraph.size() : space + 1;

            std::u32string candidate = line.empty() ? word : line + U" " + word;
            if (!line.empty() && muiTextWidth(candidate, size) > width) {
                lines.push_back(line);
                line = word;
            } else {
                line = candidate;
            }
        }
        lines.push_back(line);

        if (end == std::u32string::npos) {
            break;
        }
        start = end + 1;
    }

    return lines;
}

} // namespace fallout
