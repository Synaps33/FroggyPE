#include "Input_SF2000.h"
#include "platform/input/Keyboard.h"
#include "platform/input/Mouse.h"
#include "platform/input/Multitouch.h"
#include "platform/time.h"
#include "client/gui/components/Button.h"
#include "client/gui/Gui.h"
#include "client/gui/Screen.h"
#include "client/Minecraft.h"
#include "client/player/LocalPlayer.h"
#include "client/gui/screens/ScreenChooser.h"
#include "client/gui/screens/ArmorScreen.h"
#include "client/gui/screens/IngameBlockSelectionScreen.h"
#include "client/gui/screens/crafting/WorkbenchScreen.h"
#include "world/item/crafting/Recipe.h"
#include "world/entity/player/Inventory.h"
#include <algorithm>

namespace sf2000_input
{

static uint32_t s_currMask = 0;
static uint32_t s_prevMask = 0;
static uint32_t s_pressedMask = 0;

static int s_cursorX = 160;
static int s_cursorY = 120;

/*
 * D-Pad auto-repeat, wall-clock based so it is frame-rate independent.
 * There is no cursor step or snap radius any more: the pad moves focus between
 * controls and A presses the focused one.
 */
static const int s_cursorStep    = 12;
static const int s_snapRadius   = 40;
static const int s_repeatDelayMs = 320;
static const int s_repeatIntervalMs = 90;
static uint32_t s_repeatDir = 0;
static int s_repeatNext = 0;

/*
 * Where the last A press was delivered. The release must go to the same spot:
 * Button::clicked() hit-tests on release, so releasing at the raw cursor after
 * snapping the press onto a button centre would land outside it and the button
 * would never activate.
 */
static short s_pressX = 0;
static short s_pressY = 0;
static bool s_selectModifierUsed = false;

/*
 * Deferred key release. Minecraft drains the whole keyboard queue in one tick
 * before KeyboardInput::tick() runs, so a KEYDOWN+KEYUP pair emitted in the
 * same frame collapses and the action never fires. The release is therefore
 * emitted on the following poll().
 */
static int s_pendingKeyUp = 0;
static bool s_jumpKeyHeld = false;
static uint32_t s_consumed = 0;

/*
 * One game frame == one poll() == one pad sample, and one frame takes ~500 ms
 * at the current frame rate. A quick tap can therefore begin and end between two
 * samples and be lost entirely -- which is why SELECT (inventory) never fired
 * while the held D-Pad worked fine.
 *
 * takePress() is level triggered instead of edge triggered: a press counts as
 * seen as long as the button is observed held in at least one sample, and it
 * only fires once per press. That makes every tap action work reliably as long
 * as the button is held for at least one frame.
 */
static inline int clampPanelX(int x) { return x < 0 ? 0 : (x > 319 ? 319 : x); }
static inline int clampPanelY(int y) { return y < 0 ? 0 : (y > 239 ? 239 : y); }

static inline bool takePress(uint32_t mask, uint32_t button)
{
    if (mask & button)
    {
        if (s_consumed & button)
            return false;
        s_consumed |= button;
        return true;
    }
    s_consumed &= ~button;
    return false;
}

void setPadState(uint32_t heldMask)
{
    s_prevMask = s_currMask;
    s_currMask = heldMask;
    s_pressedMask = s_currMask & ~s_prevMask;
}

uint32_t getHeldMask() { return s_currMask; }
uint32_t getPressedMask() { return s_pressedMask; }

void getCursorPos(int* x, int* y)
{
    if (x) *x = s_cursorX;
    if (y) *y = s_cursorY;
}

void setCursorPos(int x, int y)
{
    s_cursorX = clampPanelX(x);
    s_cursorY = clampPanelY(y);
}

static inline void updateKey(int key, bool down, bool prev)
{
    if (key <= 0)
        return;
    if (down != prev)
        Keyboard::feed((unsigned char)key, down ? KeyboardAction::KEYDOWN : KeyboardAction::KEYUP);
}

void poll(Minecraft* mc)
{
    // poll() is called from the core entry point and can run before the app
    // object exists. Nothing below is meaningful without it.
    if (mc == nullptr)
        return;

    // release the key tapped on the previous frame
    if (s_pendingKeyUp != 0) {
        Keyboard::feed((unsigned char)s_pendingKeyUp, KeyboardAction::KEYUP);
        s_pendingKeyUp = 0;
    }

    uint32_t held = s_currMask;
    uint32_t prev = s_prevMask;
    uint32_t pressed = s_pressedMask;

    bool inMenu = (mc->screen != nullptr);

    if (inMenu)
    {
        // ----------------------------------------------------
        // Menu / GUI Controls
        // ----------------------------------------------------

        // Quick tab switching between Inventory, Crafting, and Armor screens
        bool isInvScreen = (dynamic_cast<IngameBlockSelectionScreen*>(mc->screen) != nullptr ||
                            dynamic_cast<WorkbenchScreen*>(mc->screen) != nullptr ||
                            dynamic_cast<ArmorScreen*>(mc->screen) != nullptr);

        if (isInvScreen)
        {
            // X button: switch to Crafting (WorkbenchScreen)
            if (takePress(held, BTN_X))
            {
                if (dynamic_cast<WorkbenchScreen*>(mc->screen) == nullptr && mc->player != nullptr && !mc->isCreativeMode())
                {
                    mc->player->startCrafting((int)mc->player->x, (int)mc->player->y,
                                              (int)mc->player->z, Recipe::SIZE_2X2);
                    mc->screen->resetFocus();
                    return;
                }
            }
            // Y button: switch to ArmorScreen
            if (takePress(held, BTN_Y))
            {
                if (dynamic_cast<ArmorScreen*>(mc->screen) == nullptr)
                {
                    mc->setScreen(new ArmorScreen());
                    mc->screen->resetFocus();
                    return;
                }
            }
            // SELECT button: switch to Inventory / Block Selection (IngameBlockSelectionScreen)
            if (takePress(held, BTN_SELECT))
            {
                if (dynamic_cast<IngameBlockSelectionScreen*>(mc->screen) == nullptr)
                {
                    mc->screenChooser.setScreen(SCREEN_BLOCKSELECTION);
                    mc->screen->resetFocus();
                    return;
                }
            }
        }

        /*
         * D-Pad navigation, in whichever mode the screen asks for:
         *
         *  GUI_NAV_CURSOR  the pad steps a free pointer (12 px per press, with
         *                  auto-repeat) and A presses the nearest control. Used
         *                  for the inventory and the options, where you browse a
         *                  layout and want to point at a specific slot.
         *  GUI_NAV_FOCUS   the pad steps between controls and A presses the
         *                  focused one. Used for grids and the world carousel.
         *
         * Auto-repeat is wall-clock based, so it behaves the same regardless of
         * how many frames a second the handheld manages.
         */
        const int now = getTimeMs();
        uint32_t dirMask =
            ((held & BTN_LEFT)  ? BTN_LEFT  : 0u) |
            ((held & BTN_RIGHT) ? BTN_RIGHT : 0u) |
            ((held & BTN_UP)    ? BTN_UP    : 0u) |
            ((held & BTN_DOWN)  ? BTN_DOWN  : 0u);

        if (dirMask == 0)
        {
            s_repeatDir = 0;
            s_repeatNext = 0;
        }
        else if (dirMask != s_repeatDir)
        {
            s_repeatDir = dirMask;          // fresh press: act at once
            s_repeatNext = now + s_repeatDelayMs;
        }
        else if (now >= s_repeatNext)
        {
            s_repeatNext = now + s_repeatIntervalMs;
        }
        else
        {
            dirMask = 0;                    // still waiting for the repeat delay
        }

        const int sx = (dirMask & BTN_LEFT)  ? -1 :
                       (dirMask & BTN_RIGHT) ?  1 : 0;
        const int sy = (dirMask & BTN_UP)    ? -1 :
                       (dirMask & BTN_DOWN)  ?  1 : 0;
        const bool focusMode = (mc->screen->getNavMode() == GUI_NAV_FOCUS);

        if (sx != 0 || sy != 0)
        {
            if (focusMode)
            {
                /*
                 * Left/Right on a focused value control changes the value instead
                 * of moving away from it -- otherwise changing a setting would
                 * mean leaving it first.
                 */
                const bool horizontal = (sx != 0);
                if (mc->screen->focusDirection(sx, sy))
                {
                    // the screen handled it internally
                }
                else if (!(horizontal && mc->screen->adjustFocusedSlider(sx)))
                {
                    if (!mc->screen->moveFocus(sx, sy))
                    {
                        // Nothing that way: wrap, so a held direction keeps cycling.
                        mc->screen->setFocusIndex(-1);
                        if (mc->screen->moveFocus(sx, sy) && horizontal)
                            mc->screen->adjustFocusedSlider(sx);
                    }
                }

                mc->screen->refreshFocus();

                // Park the pointer on the focused control. Some widgets (the
                // container panes) read Mouse::getX() directly instead of going
                // through the event queue, and a stale position there made them
                // highlight the wrong cell.
                const std::vector<FocusTarget>& ft = mc->screen->getFocusTargets();
                const int fi = mc->screen->getFocusIndex();
                if (fi >= 0 && fi < (int)ft.size())
                {
                    s_cursorX = clampPanelX((int)(ft[fi].centreX() / Gui::InvGuiScale));
                    s_cursorY = clampPanelY((int)(ft[fi].centreY() / Gui::InvGuiScale));
                    Mouse::feed(MouseAction::ACTION_MOVE, 0, (short)s_cursorX, (short)s_cursorY, 0, 0);
                    Multitouch::feed(0, 0, (float)s_cursorX, (float)s_cursorY, 0);
                }
            }
            else
            {
                const int step = s_cursorStep;
                int dx = sx * step;
                int dy = sy * step;
                s_cursorX = clampPanelX(s_cursorX + dx);
                s_cursorY = clampPanelY(s_cursorY + dy);
                Mouse::feed(MouseAction::ACTION_MOVE, 0, (short)s_cursorX, (short)s_cursorY, (short)dx, (short)dy);
                Multitouch::feed(0, 0, (float)s_cursorX, (float)s_cursorY, 0);
            }
        }

        /*
         * A = press.
         *
         * Focus mode: press the focused control.
         * Cursor mode: press the nearest control to the pointer, so activation
         * does not depend on landing inside a small target.
         *
         * Both convert from the GUI's logical coordinates to physical panel
         * pixels; at the 1.2x default the two spaces differ by 20%.
         */
        bool a_now = (held & BTN_A) != 0;
        bool a_was = (prev & BTN_A) != 0;
        if (a_now != a_was)
        {
            short clickX = (short)s_cursorX;
            short clickY = (short)s_cursorY;

            if (a_now)
            {
                const float invScale = Gui::InvGuiScale;

                if (focusMode)
                {
                    mc->screen->refreshFocus();
                    const std::vector<FocusTarget>& ft = mc->screen->getFocusTargets();
                    const int fi = mc->screen->getFocusIndex();
                    if (fi >= 0 && fi < (int)ft.size())
                    {
                        clickX = (short)(ft[fi].centreX() / invScale);
                        clickY = (short)(ft[fi].centreY() / invScale);
                    }
                }
                else
                {
                    int best = -1;
                    int bestDist = s_snapRadius * s_snapRadius;
                    const std::vector<Button*>& bs = mc->screen->getButtons();
                    for (size_t i = 0; i < bs.size(); ++i)
                    {
                        Button* b = bs[i];
                        const int cx = (int)((b->x + b->width  / 2) / invScale);
                        const int cy = (int)((b->y + b->height / 2) / invScale);
                        const int ddx = cx - s_cursorX;
                        const int ddy = cy - s_cursorY;
                        const int d2 = ddx * ddx + ddy * ddy;
                        if (d2 < bestDist) { bestDist = d2; best = (int)i; }
                    }
                    if (best >= 0)
                    {
                        Button* b = bs[best];
                        clickX = (short)((b->x + b->width  / 2) / invScale);
                        clickY = (short)((b->y + b->height / 2) / invScale);
                    }
                }

                s_pressX = clickX;
                s_pressY = clickY;
            }
            else
            {
                // Release exactly where the press was delivered: Button::clicked()
                // hit-tests on release, so releasing at a different spot lands
                // outside the control and it never activates.
                clickX = s_pressX;
                clickY = s_pressY;
            }

            Mouse::feed(MouseAction::ACTION_LEFT, a_now ? MouseAction::DATA_DOWN : MouseAction::DATA_UP,
                        clickX, clickY);
            Multitouch::feed(1, a_now ? 1 : 0, (float)clickX, (float)clickY, 0);
        }

        // B button = Back / Cancel
        if (pressed & BTN_B)
        {
            mc->handleBack(false);
        }

        // START = Close / Resume
        if (pressed & BTN_START)
        {
            mc->setScreen(nullptr);
        }
    }
    else
    {
        // ----------------------------------------------------
        // In-Game Controls
        // ----------------------------------------------------
        bool selectHeld = (held & BTN_SELECT) != 0;
        bool selectPressed = (pressed & BTN_SELECT) != 0;
        bool selectReleased = ((prev & BTN_SELECT) != 0) && !selectHeld;

        /*
         * Key codes must come from the option bindings. KeyboardInput and
         * Gui::handleKeyPressed compare incoming codes against options->keyUp.key
         * and friends; on SF2000 those are 19/20/21/22/23, not ASCII 87/83/65/68.
         * Feeding raw ASCII made movement and jumping silently do nothing.
         */
        const Options* opt = &mc->options;
        const int kKeyUp   = opt->keyUp.key;
        const int kKeyDown = opt->keyDown.key;
        const int kKeyJump = opt->keyJump.key;
        const int kKeyCraft = opt->keyCraft.key;
        const int kKeyDrop = opt->keyDrop.key;

        // One-shot key: press now, release on the next poll() (see above).
        auto tapKey = [&](int key)
        {
            if (key <= 0) return;
            Keyboard::feed((unsigned char)key, KeyboardAction::KEYDOWN);
            s_pendingKeyUp = key;
        };

        if (takePress(held, BTN_SELECT))
        {
            s_selectModifierUsed = false;

            // Direct chords: if a chord button is held at the same time as SELECT
            if (held & BTN_X)
            {
                s_selectModifierUsed = true;
                if (mc->player != nullptr && !mc->isCreativeMode())
                    mc->player->startCrafting((int)mc->player->x, (int)mc->player->y,
                                              (int)mc->player->z, Recipe::SIZE_2X2);
                else
                    mc->screenChooser.setScreen(SCREEN_BLOCKSELECTION);
                if (mc->screen) mc->screen->resetFocus();
                return;
            }
            else if (held & BTN_Y)
            {
                s_selectModifierUsed = true;
                mc->setScreen(new ArmorScreen());
                if (mc->screen) mc->screen->resetFocus();
                return;
            }
            else if (held & BTN_DOWN)
            {
                s_selectModifierUsed = true;
                if (mc->player != nullptr && mc->player->inventory != nullptr)
                    mc->player->inventory->dropSlot(mc->player->inventory->selected, false);
                else
                    tapKey(kKeyDrop);
                return;
            }
            else if (held & BTN_UP)
            {
                s_selectModifierUsed = true;
                mc->screenChooser.setScreen(SCREEN_BLOCKSELECTION);
                if (mc->screen) mc->screen->resetFocus();
                return;
            }

            /*
             * Variant 1:
             * In Creative mode, or in Survival with Auto Jump ON, SELECT opens
             * the full inventory / block selection screen directly.
             * From there, the player can also jump to Crafting (X) or Armor (Y).
             *
             * When Auto Jump is switched off, the player needs a manual jump and
             * SELECT becomes jump, while inventory moves to chords (SELECT + UP / X / Y).
             */
            if (opt->autoJump || mc->isCreativeMode())
            {
                mc->screenChooser.setScreen(SCREEN_BLOCKSELECTION);
                if (mc->screen) mc->screen->resetFocus();
                return;
            }
            else
            {
                Keyboard::feed((unsigned char)kKeyJump, KeyboardAction::KEYDOWN);
                s_jumpKeyHeld = true;
            }
        }

        if (selectHeld)
        {
            // SELECT + button means SELECT was used as a modifier: cancel the
            // jump edge so it does not leak into the chord.
            if (pressed & (BTN_LEFT | BTN_RIGHT | BTN_UP | BTN_DOWN | BTN_X | BTN_Y | BTN_A | BTN_B))
            {
                if (!s_selectModifierUsed)
                {
                    s_selectModifierUsed = true;
                    if (s_jumpKeyHeld)
                    {
                        Keyboard::feed((unsigned char)kKeyJump, KeyboardAction::KEYUP);
                        s_jumpKeyHeld = false;
                    }
                }
            }

            // SELECT + D-Pad Up: inventory (when autoJump=false)
            if (takePress(held, BTN_UP))
            {
                mc->screenChooser.setScreen(SCREEN_BLOCKSELECTION);
                if (mc->screen) mc->screen->resetFocus();
                return;
            }

            // SELECT + X: crafting
            if (takePress(held, BTN_X))
            {
                if (mc->player != nullptr && !mc->isCreativeMode())
                    mc->player->startCrafting((int)mc->player->x, (int)mc->player->y,
                                              (int)mc->player->z, Recipe::SIZE_2X2);
                else
                    mc->screenChooser.setScreen(SCREEN_BLOCKSELECTION);
                if (mc->screen) mc->screen->resetFocus();
                return;
            }

            // SELECT + Y: armor screen
            if (takePress(held, BTN_Y))
            {
                mc->setScreen(new ArmorScreen());
                if (mc->screen) mc->screen->resetFocus();
                return;
            }

            // SELECT + D-Pad Down: drop item
            if (takePress(held, BTN_DOWN))
            {
                if (mc->player != nullptr && mc->player->inventory != nullptr)
                    mc->player->inventory->dropSlot(mc->player->inventory->selected, false);
                else
                    tapKey(kKeyDrop);
            }
        }
        else
        {
            /*
             * Movement layout for the D-Pad:
             *   Up / Down    -> walk forward / backward
             *   Left / Right -> hotbar (inventory) navigation
             *
             * Sidestepping used to be WASD on Left/Right, which fought with the
             * inventory bar and made precise block placement awkward.
             */
            updateKey(kKeyUp,   (held & BTN_UP) != 0,   (prev & BTN_UP) != 0);
            updateKey(kKeyDown, (held & BTN_DOWN) != 0, (prev & BTN_DOWN) != 0);

            // Never hold a stale strafe key from the previous layout
            updateKey(opt->keyLeft.key,  false, (prev & BTN_LEFT) != 0);
            updateKey(opt->keyRight.key, false, (prev & BTN_RIGHT) != 0);

            // Inventory bar navigation. The real inventory selection is the
            // source of truth: a cached counter desyncs whenever the slot is
            // changed by any other means (number keys, scroll wheel, pickup).
            if (mc->player != nullptr &&
                (takePress(held, BTN_LEFT) || takePress(held, BTN_RIGHT)))
            {
                const int dir = (held & BTN_LEFT) ? -1 : 1;
                const int slot = (mc->player->inventory->selected + dir + 9) % 9;
                mc->player->inventory->selectSlot(slot);
            }
        }

        // release the manual jump edge when SELECT is let go (Auto Jump off)
        if (s_jumpKeyHeld && selectReleased) {
            Keyboard::feed((unsigned char)kKeyJump, KeyboardAction::KEYUP);
            s_jumpKeyHeld = false;
        }

        // Camera rotation via Face buttons: X, B, Y, A (only when SELECT is not held as modifier)
        short camDx = 0;
        short camDy = 0;
        const short camSpeedX = 5;
        const short camSpeedY = 4;

        if (!selectHeld)
        {
            if (held & BTN_X) camDy -= camSpeedY; // Look up
            if (held & BTN_B) camDy += camSpeedY; // Look down
            if (held & BTN_Y) camDx -= camSpeedX; // Look left
            if (held & BTN_A) camDx += camSpeedX; // Look right

            if (camDx != 0 || camDy != 0)
            {
                Mouse::feed(0, 0, 160, 120, camDx, camDy);
            }
        }

        // START = Pause / Menu
        if (takePress(held, BTN_START))
        {
            mc->pauseGame(false);
        }
    }

    /*
     * Shoulder button edges are fed whether or not a screen is open.
     *
     * These used to live in the in-game branch only, which meant the *release*
     * was dropped whenever the press had opened a screen -- right-clicking a
     * crafting table opened WorkbenchScreen and then the branch changed, so
     * ACTION_RIGHT was never released. Mouse::isButtonDown(ACTION_RIGHT) stayed
     * true for good, Minecraft::handleBuildAction kept running every tick and
     * reopened the crafting screen, and there was no way out of it.
     *
     * The same applies to L: breaking a block that opens a screen would leave the
     * left button stuck down.
     */
    {
        const bool l_now = (held & BTN_L) != 0;
        const bool l_was = (prev & BTN_L) != 0;
        if (l_now != l_was)
            Mouse::feed(MouseAction::ACTION_LEFT, l_now ? MouseAction::DATA_DOWN : MouseAction::DATA_UP, 160, 120);

        const bool r_now = (held & BTN_R) != 0;
        const bool r_was = (prev & BTN_R) != 0;
        if (r_now != r_was)
            Mouse::feed(MouseAction::ACTION_RIGHT, r_now ? MouseAction::DATA_DOWN : MouseAction::DATA_UP, 160, 120);
    }
}

} // namespace sf2000_input
