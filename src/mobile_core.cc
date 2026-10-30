#include "mobile_contracts.h"
#include "bottom_screen.h"
#include "plib/gnw/text.h"
#include <deque>
#include <utility>
namespace fallout {
static MobileSnapshot snapshot;
static std::deque<MobileCommand> commands;
const MobileSnapshot& mobileSnapshot() { return snapshot; }
void mobilePublishSnapshot(MobileSnapshot next) {
    if (next.generation != snapshot.generation) commands.clear();
    snapshot = std::move(next);
}
bool mobileQueueCommand(const MobileCommand& command) {
    if (command.generation != snapshot.generation || commands.size() >= 64) return false;
    commands.push_back(command);
    return true;
}
bool mobileTakeCommand(MobileCommand* command) {
    if (!command || commands.empty()) return false;
    *command = commands.front();
    commands.pop_front();
    return true;
}
void mobileClearCommands() { commands.clear(); }
void mobileRenderBottom() {
    int width, height;
    bottomScreenGetSize(&width, &height);
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) return;
    const char* label = "Fallout 1 CE - bottom screen";
    int oldFont = text_curr();
    text_font(100);
    int w = text_width(label), h = text_height();
    std::vector<unsigned char> mask(w * h, 0);
    text_to_buf(mask.data(), label, w, w, 1);
    text_font(oldFont);
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4, 0);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        auto i = (static_cast<size_t>(y) * width + x) * 4;
        int tx = x - (width - w) / 2, ty = y - (height - h) / 2;
        pixels[i + 1] = tx >= 0 && tx < w && ty >= 0 && ty < h && mask[ty * w + tx] ? 220 : 16;
        pixels[i + 3] = 255;
    }
    bottomScreenPostFrame(pixels.data(), width, height, width * 4);
}
}
