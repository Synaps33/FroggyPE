#ifndef NET_MINECRAFT_CLIENT_GUI__Screen_H__
#define NET_MINECRAFT_CLIENT_GUI__Screen_H__

//package net.minecraft.client.gui;

#include <vector>
#include "GuiComponent.h"
#include "../Options.h"

class Font;
class Minecraft;
class Button;
class TextBox;
struct IntRectangle;

/*
 * One stop of D-Pad focus navigation, in the GUI's logical coordinates.
 *
 * The pad moves focus between these instead of driving a free-floating cursor:
 * at the frame rate this handheld manages, aiming a cursor at small targets was
 * unusable. Each target is activated by clicking its centre, which reuses the
 * ordinary mouse path so every screen keeps working without a cursor.
 */
struct FocusTarget
{
	int x, y, w, h;

	// Non-NULL for a step slider: Left/Right then changes its value instead of
	// moving focus, which is what a console user expects from a value control.
	const Options::Option* sliderOption;

	// The widget this target drives, when there is one.
	Button* button;

	// Free slot for screens with a grid: the item/slot index this target is.
	// -1 when not used.
	int id;

	FocusTarget() : x(0), y(0), w(0), h(0), sliderOption(NULL), button(NULL), id(-1) {}
	FocusTarget(int x_, int y_, int w_, int h_, Button* b = NULL, const Options::Option* o = NULL)
		: x(x_), y(y_), w(w_), h(h_), sliderOption(o), button(b), id(-1) {}

	int centreX() const { return x + w / 2; }
	int centreY() const { return y + h / 2; }
};

/*
 * How the D-Pad drives a screen.
 *
 * GUI_NAV_CURSOR  the pad moves a free pointer and A presses the control it
 *                 snaps to. Right for browsing a spatial layout, where you want
 *                 to point at a specific slot or slider.
 * GUI_NAV_FOCUS    the pad steps between discrete controls and A presses the
 *                 focused one. Right for grids and carousels, where pointing is
 *                 pointless because the interesting targets are laid out in a
 *                 regular matrix.
 */
enum GuiNavMode
{
	GUI_NAV_CURSOR = 0,
	GUI_NAV_FOCUS  = 1
};

class Screen: public GuiComponent
{
public:
	Screen();

    virtual void render(int xm, int ym, float a);

    void init(Minecraft* minecraft, int width, int height);
	virtual void init();

    void setSize(int width, int height);
	virtual void setupPositions() {};

	virtual void updateEvents();
    virtual void mouseEvent();
    virtual void keyboardEvent();
	virtual void keyboardTextEvent();
	virtual bool handleBackEvent(bool isDown);

    virtual void tick() {}

    virtual void removed() {}

    virtual void renderBackground();
    virtual void renderBackground(int vo);
    virtual void renderDirtBackground(int vo);
	// query
	virtual bool renderGameBehind();
	virtual bool hasClippingArea(IntRectangle& out);

    virtual bool isPauseScreen();
	virtual bool isErrorScreen();
	virtual bool isInGameScreen();
    virtual bool closeOnPlayerHurt();

    virtual void confirmResult(bool result, int id) {}
	virtual void lostFocus();
	virtual void toGUICoordinate(int& x, int& y);
protected:
	void updateTabButtonSelection();

	virtual void buttonClicked(Button* button) {}
	virtual void mouseClicked(int x, int y, int buttonNum);
	virtual void mouseReleased(int x, int y, int buttonNum);

	virtual void keyPressed(int eventKey);
	virtual void keyboardNewChar(char inputChar) {}
public:
	int width;
	int height;
	bool passEvents;

	/*
	 * Used by the SF2000 gamepad navigation to press the button nearest to the
	 * virtual cursor instead of relying on the cursor landing inside it exactly.
	 */
	const std::vector<Button*>& getButtons() const { return buttons; }

	/*
	 * D-Pad focus navigation. collectFocusTargets() is overridden by screens
	 * whose interactive parts are not plain buttons (option sliders, inventory
	 * grids, container panes). The base implementation covers every Button the
	 * screen registered, which is most of them.
	 */
	virtual void collectFocusTargets(std::vector<FocusTarget>& out);

	virtual int getNavMode() const { return GUI_NAV_CURSOR; }

	// Move focus one step. Returns false when there is nowhere to go, so the
	// caller can fall back to something else.
	bool moveFocus(int dx, int dy);

	// Adjust the focused step slider by one step. Returns false if the focus is
	// not on a slider. Screens that own value controls override this: the widget
	// holding the value is not reachable from the base screen.
	virtual bool adjustFocusedSlider(int dir);

	int  getFocusIndex() const { return focusIndex; }
	void setFocusIndex(int i) { focusIndex = i; }
	void resetFocus() { focusIndex = -1; }
	const std::vector<FocusTarget>& getFocusTargets() const { return focusTargets; }

	// Re-read the target list, keeping the focus on the same widget if possible.
	void refreshFocus();

	// Called after the focus index changed, so a screen can mirror it onto its
	// own selection state.
	virtual void focusMoved() {}

	/*
	 * Called for a directional press that landed on the focused target, before
	 * the focus would be moved. Lets a screen handle the direction itself -- the
	 * world carousel steps between worlds on Left/Right instead of losing focus.
	 * Return true if the press was consumed.
	 */
	virtual bool focusDirection(int dx, int dy) { (void)dx; (void)dy; return false; }

protected:
	std::vector<FocusTarget> focusTargets;
	int focusIndex;

	Minecraft* minecraft;
	std::vector<Button*> buttons;
	std::vector<TextBox*> textBoxes;

	std::vector<Button*> tabButtons;
	int tabButtonIndex;

	Font* font;
private:
	Button* clickedButton;
};

#endif /*NET_MINECRAFT_CLIENT_GUI__Screen_H__*/
