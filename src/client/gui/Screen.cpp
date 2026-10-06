#include "Screen.h"
#include "components/Button.h"
#include "components/Slider.h"
#include "components/TextBox.h"
#include "../Options.h"
#include "../Minecraft.h"
#include "../renderer/Tesselator.h"
#include "../sound/SoundEngine.h"
#include "../../platform/input/Keyboard.h"
#include "../../platform/input/Mouse.h"
#include "../renderer/Textures.h"

Screen::Screen()
:   passEvents(false),
	clickedButton(NULL),
	focusIndex(-1),
	tabButtonIndex(0),
	width(1),
	height(1),
	minecraft(NULL),
	font(NULL)
{
}

/*
 * D-Pad focus navigation.
 *
 * Every registered Button is a focus stop by default. Screens override
 * collectFocusTargets() to add controls that are not buttons -- option sliders
 * and toggles live in the option pane tree, inventory screens have a slot grid,
 * container screens have a scrolling pane.
 */
void Screen::collectFocusTargets(std::vector<FocusTarget>& out)
{
	for (size_t i = 0; i < buttons.size(); ++i)
	{
		Button* b = buttons[i];
		if (!b->visible || !b->active) continue;
		out.push_back(FocusTarget(b->x, b->y, b->width, b->height, b));
	}
}

void Screen::refreshFocus()
{
	// Remember where the focus was, by widget identity where possible.
	Button* wasButton = NULL;
	int wasId = -1;
	int wasX = 0, wasY = 0;
	if (focusIndex >= 0 && focusIndex < (int)focusTargets.size())
	{
		wasButton = focusTargets[focusIndex].button;
		wasId    = focusTargets[focusIndex].id;
		wasX = focusTargets[focusIndex].x;
		wasY = focusTargets[focusIndex].y;
	}

	focusTargets.clear();
	collectFocusTargets(focusTargets);

	if (focusTargets.empty())
	{
		focusIndex = -1;
		return;
	}


	if (wasButton != NULL)
	{
		for (size_t i = 0; i < focusTargets.size(); ++i)
			if (focusTargets[i].button == wasButton) { focusIndex = (int)i; focusMoved(); return; }
	}

	/*
	 * Grid targets have no widget, so they are matched by id. This matters when
	 * the target list is rebuilt after the pane scrolled: the same recipe is at a
	 * different offset, and matching by position would slide the focus onto
	 * whatever moved under it.
	 */
	if (wasButton == NULL && wasId >= 0)
	{
		for (size_t i = 0; i < focusTargets.size(); ++i)
			if (focusTargets[i].button == NULL && focusTargets[i].id == wasId)
			{ focusIndex = (int)i; focusMoved(); return; }
	}

	// No widget identity (a grid target, say): keep the nearest position.
	if (wasX || wasY)
	{
		int best = -1, bestD = 1 << 30;
		for (size_t i = 0; i < focusTargets.size(); ++i)
		{
			const int d = (focusTargets[i].centreX() - wasX) * (focusTargets[i].centreX() - wasX)
			            + (focusTargets[i].centreY() - wasY) * (focusTargets[i].centreY() - wasY);
			if (d < bestD) { bestD = d; best = (int)i; }
		}
		focusIndex = best;
		focusMoved();
		return;
	}

	focusIndex = 0;
	focusMoved();
}

/*
 * Pick the stop to move to.
 *
 * Candidates must lie in the pressed direction: their leading edge has to be
 * beyond the current one, so focus never sticks. Among those, the score is the
 * distance along the pressed axis plus a penalty for sideways offset, which
 * makes the pad walk a column of sliders straight up and down while still
 * jumping across to a neighbouring column when that is what was asked for.
 */
bool Screen::moveFocus(int dx, int dy)
{
	refreshFocus();
	if (focusTargets.empty())
		return false;

	if (focusIndex < 0 || focusIndex >= (int)focusTargets.size())
	{
		focusIndex = 0;
		focusMoved();
		return true;
	}

	const FocusTarget& cur = focusTargets[focusIndex];
	const int ccx = cur.centreX(), ccy = cur.centreY();

	int best = -1;
	int bestScore = 1 << 30;

	for (size_t i = 0; i < focusTargets.size(); ++i)
	{
		if ((int)i == focusIndex) continue;
		const FocusTarget& t = focusTargets[i];
		const int tcx = t.centreX(), tcy = t.centreY();

		const int ddx = tcx - ccx;
		const int ddy = tcy - ccy;

		int along, across;
		if (dx != 0)
		{
			if ((ddx > 0 ? dx : -dx) <= 0) continue;   // wrong way
			along  = (ddx > 0 ? dx : -dx) * ddx;
			across = ddy;
		}
		else
		{
			if ((ddy > 0 ? dy : -dy) <= 0) continue;   // wrong way
			along  = (ddy > 0 ? dy : -dy) * ddy;
			across = ddx;
		}

		const int score = along + 2 * (across < 0 ? -across : across);
		if (score < bestScore)
		{
			bestScore = score;
			best = (int)i;
		}
	}

	if (best < 0)
		return false;

	focusIndex = best;
	focusMoved();
	return true;
}

bool Screen::adjustFocusedSlider(int dir)
{
	return false;
}

void Screen::render( int xm, int ym, float a )
{
	for (unsigned int i = 0; i < buttons.size(); i++) {
		Button* button = buttons[i];
		button->render(minecraft, xm, ym);
	}

	// render any text boxes after buttons
	for (unsigned int i = 0; i < textBoxes.size(); i++) {
		TextBox* textbox = textBoxes[i];
		textbox->render(minecraft, xm, ym);
	}
}

void Screen::init( Minecraft* minecraft, int width, int height )
{
	//particles = /*new*/ GuiParticles(minecraft);
	this->minecraft = minecraft;
	this->font = minecraft->font;
	this->width = width;
	this->height = height;
	init();
	setupPositions();
	updateTabButtonSelection();
}

void Screen::init()
{
}

void Screen::setSize( int width, int height )
{
	this->width = width;
	this->height = height;
	setupPositions();
}

bool Screen::handleBackEvent( bool isDown )
{
	return false;
}

void Screen::updateEvents()
{
	if (passEvents)
		return;

	while (Mouse::next())
		mouseEvent();

	while (Keyboard::next())
		keyboardEvent();
	while (Keyboard::nextTextChar())
		keyboardTextEvent();
}

void Screen::mouseEvent()
{
	const MouseAction& e = Mouse::getEvent();
	if (!e.isButton())
		return;

	if (Mouse::getEventButtonState()) {
		int xm = e.x * width / minecraft->width;
		int ym = e.y * height / minecraft->height - 1;
		mouseClicked(xm, ym, Mouse::getEventButton());
	} else {
		int xm = e.x * width / minecraft->width;
		int ym = e.y * height / minecraft->height - 1;
		mouseReleased(xm, ym, Mouse::getEventButton());
	}
}

void Screen::keyboardEvent()
{
	if (Keyboard::getEventKeyState()) {
		//if (Keyboard.getEventKey() == Keyboard.KEY_F11) {
		//    minecraft->toggleFullScreen();
		//    return;
		//}
		keyPressed(Keyboard::getEventKey());
	}
}
void Screen::keyboardTextEvent()
{
	keyboardNewChar(Keyboard::getChar());
}
void Screen::renderBackground()
{
	renderBackground(0);
}

void Screen::renderBackground( int vo )
{
	if (minecraft->isLevelGenerated()) {
		fillGradient(0, 0, width, height, 0xc0101010, 0xd0101010);
	} else {
		renderDirtBackground(vo);
	}
}

void Screen::renderDirtBackground( int vo )
{
	//glDisable2(GL_LIGHTING);
	glDisable2(GL_FOG);
	Tesselator& t = Tesselator::instance;
	minecraft->textures->loadAndBindTexture("gui/background.png");
	glColor4f2(1, 1, 1, 1);
	float s = 32;
	float fvo = (float) vo;
	t.begin();
	t.color(0x404040);
	t.vertexUV(0, (float)height, 0, 0, height / s + fvo);
	t.vertexUV((float)width, (float)height, 0, width / s, (float)height / s + fvo);
	t.vertexUV((float)width, 0, 0, (float)width / s, 0 + fvo);
	t.vertexUV(0, 0, 0, 0, 0 + fvo);
	t.draw();
}

bool Screen::isPauseScreen()
{
	return true;
}

bool Screen::isErrorScreen()
{
	return false;
}

bool Screen::isInGameScreen()
{
	return true;
}

bool Screen::closeOnPlayerHurt() {
    return false;
}

void Screen::keyPressed( int eventKey )
{
	if (eventKey == Keyboard::KEY_ESCAPE) {
		minecraft->setScreen(NULL);
		//minecraft->grabMouse();
	}

	// pass key events to any text boxes first
	for (auto& textbox : textBoxes) {
		textbox->handleKey(eventKey);
	}

	if (minecraft->useTouchscreen())
		return;

	// "Tabbing" the buttons (walking with keys)
	const int tabButtonCount = tabButtons.size();
	if (!tabButtonCount)
		return;

	Options& o = minecraft->options;
	if (eventKey == o.keyMenuNext.key)
		if (++tabButtonIndex == tabButtonCount) tabButtonIndex = 0;
	if (eventKey == o.keyMenuPrevious.key)
		if (--tabButtonIndex == -1) tabButtonIndex = tabButtonCount-1;
	if (eventKey == o.keyMenuOk.key) {
		Button* button = tabButtons[tabButtonIndex];
		if (button->active) {
			minecraft->soundEngine->playUI("random.click", 1, 1);
			buttonClicked(button);
		}
	}

	updateTabButtonSelection();
}

void Screen::updateTabButtonSelection()
{
	if (minecraft->useTouchscreen())
		return;

	for (unsigned int i = 0; i < tabButtons.size(); ++i)
		tabButtons[i]->selected = (i == tabButtonIndex);
}

void Screen::mouseClicked( int x, int y, int buttonNum )
{
	if (buttonNum == MouseAction::ACTION_LEFT) {
		for (unsigned int i = 0; i < buttons.size(); ++i) {
			Button* button = buttons[i];
            //LOGI("Hit-testing button: %p\n", button);
			if (button->clicked(minecraft, x, y)) {
                button->setPressed();

                //LOGI("Hit-test successful: %p\n", button);
				clickedButton = button;
/*
#if !defined(ANDROID) && !defined(__APPLE__) //if (!minecraft->isTouchscreen()) {
					minecraft->soundEngine->playUI("random.click", 1, 1);
					buttonClicked(button);
#endif }
*/
			}
		}
	}

	// let textboxes see the click regardless
	for (auto& textbox : textBoxes) {
		textbox->mouseClicked(minecraft, x, y, buttonNum);
	}
}

void Screen::mouseReleased( int x, int y, int buttonNum )
{
	// Take ownership of the pressed-button state up front. buttonClicked() may
	// call setScreen(), which deletes this Screen and its buttons, so nothing
	// below may touch `this` or `clickedButton` after that call.
	Button* pressed = clickedButton;
	clickedButton = NULL;

	if (!pressed || buttonNum != MouseAction::ACTION_LEFT) return;

	for (unsigned int i = 0; i < buttons.size(); ++i) {
		Button* button = buttons[i];
		if (pressed == button && button->clicked(minecraft, x, y)) {
			// released() only clears the pressed flag; do it while `this` is alive
			button->released(x, y);
			minecraft->soundEngine->playUI("random.click", 1, 1);
			buttonClicked(button);
			return; // `this` may be deleted from here on
		}
	}
}

bool Screen::renderGameBehind() {
	return true;
}

bool Screen::hasClippingArea( IntRectangle& out )
{
	return false;
}

void Screen::lostFocus() {
	for(std::vector<TextBox*>::iterator it = textBoxes.begin(); it != textBoxes.end(); ++it) {
		TextBox* tb = *it;
		tb->loseFocus(minecraft);
	}
}

void Screen::toGUICoordinate( int& x, int& y ) {
	x = x * width / minecraft->width;
	y = y * height / minecraft->height - 1;
}
