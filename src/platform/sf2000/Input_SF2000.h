#pragma once

#include <cstdint>

class Minecraft;

namespace sf2000_input
{

enum JoypadMask : uint32_t
{
    BTN_B      = 1u << 0,
    BTN_Y      = 1u << 1,
    BTN_SELECT = 1u << 2,
    BTN_START  = 1u << 3,
    BTN_UP     = 1u << 4,
    BTN_DOWN   = 1u << 5,
    BTN_LEFT   = 1u << 6,
    BTN_RIGHT  = 1u << 7,
    BTN_A      = 1u << 8,
    BTN_X      = 1u << 9,
    BTN_L      = 1u << 10,
    BTN_R      = 1u << 11,
};

void setPadState(uint32_t heldMask);
uint32_t getHeldMask();
uint32_t getPressedMask();

// Virtual menu cursor, in physical panel pixels. Exposed for the automated tests.
void getCursorPos(int* x, int* y);
void setCursorPos(int x, int y);

void poll(Minecraft* mc);

} // namespace sf2000_input
