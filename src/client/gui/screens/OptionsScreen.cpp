#include "OptionsScreen.h"

#include "StartMenuScreen.h"
#include "PauseScreen.h"
#include "UsernameScreen.h"
#include "DialogDefinitions.h"
#include "../../Minecraft.h"
#include "../../../AppPlatform.h"
#include "../../../platform/input/Keyboard.h"
#include "CreditsScreen.h"

#include "../components/OptionsPane.h"
#include "../components/ImageButton.h"
#include "../components/OptionsGroup.h"
#include "../components/OptionsItem.h"
#include "../components/Slider.h"

OptionsScreen::OptionsScreen()
	: btnClose(NULL),
	bHeader(NULL),
	btnChangeUsername(NULL),
	btnCredits(NULL),
	selectedCategory(0) {
}

OptionsScreen::~OptionsScreen() {

	if (btnClose != NULL) {
		delete btnClose;
		btnClose = NULL;
	}

	if (bHeader != NULL) {
		delete bHeader;
		bHeader = NULL;
	}

	if (btnChangeUsername != NULL) {
		delete btnChangeUsername;
		btnChangeUsername = NULL;
	}

	if (btnCredits != NULL) {
		delete btnCredits;
		btnCredits = NULL;
	}

	for (std::vector<Touch::TButton*>::iterator it = categoryButtons.begin(); it != categoryButtons.end(); ++it) {
		if (*it != NULL) {
			delete* it;
			*it = NULL;
		}
	}

	for (std::vector<OptionsPane*>::iterator it = optionPanes.begin(); it != optionPanes.end(); ++it) {
		if (*it != NULL) {
			delete* it;
			*it = NULL;
		}
	}

	categoryButtons.clear();
}

void OptionsScreen::init() {

	bHeader = new Touch::THeader(0, "Options");

	btnClose = new ImageButton(1, "");

	ImageDef def;
	def.name = "gui/touchgui.png";
	def.width = 34;
	def.height = 26;

	def.setSrc(IntRectangle(150, 0, (int)def.width, (int)def.height));
	btnClose->setImageDef(def, true);

	// One button per pane, in the same order as optionPanes below. The ids are
	// consecutive from 2 and buttonClicked() matches by pointer, not by a
	// hard-coded range.
	categoryButtons.push_back(new Touch::TButton(2, "Game"));
	categoryButtons.push_back(new Touch::TButton(3, "Controls"));
	categoryButtons.push_back(new Touch::TButton(4, "Graphics"));
	categoryButtons.push_back(new Touch::TButton(5, "Sound"));

	btnChangeUsername = new Button(10, "Username");
	btnCredits = new Button(11, "Credits");

	buttons.push_back(bHeader);
	buttons.push_back(btnClose);
	if (minecraft->level == NULL) {
		buttons.push_back(btnChangeUsername);
	}
	buttons.push_back(btnCredits);

	for (std::vector<Touch::TButton*>::iterator it = categoryButtons.begin(); it != categoryButtons.end(); ++it) {
		buttons.push_back(*it);
		tabButtons.push_back(*it);
	}

	generateOptionScreens();
	// start with first category selected
	selectCategory(0);
}

void OptionsScreen::setupPositions() {

	int buttonHeight = btnClose->height;

	btnClose->x = width - btnClose->width;
	btnClose->y = 0;

	int offsetNum = 1;

	for (std::vector<Touch::TButton*>::iterator it = categoryButtons.begin(); it != categoryButtons.end(); ++it) {

		(*it)->x = 0;
		(*it)->y = offsetNum * buttonHeight;
		(*it)->selected = false;

		offsetNum++;
	}

	bHeader->x = 0;
	bHeader->y = 0;
	bHeader->width = width - btnClose->width;
	bHeader->height = btnClose->height;

	// Username button (bottom-left)
	if (btnChangeUsername != NULL) {

		btnChangeUsername->width = categoryButtons.empty() ? 80 : categoryButtons[0]->width;
		btnChangeUsername->height = btnClose->height;

		btnChangeUsername->x = 0;
		btnChangeUsername->y = height - btnChangeUsername->height;
	}

	// Credits button (bottom-right)
	if (btnCredits != NULL) {

		btnCredits->width = btnChangeUsername->width;
		btnCredits->height = btnChangeUsername->height;

		btnCredits->x = width - btnCredits->width;
		btnCredits->y = height - btnCredits->height;
	}

	for (std::vector<OptionsPane*>::iterator it = optionPanes.begin(); it != optionPanes.end(); ++it) {

		if (categoryButtons.size() > 0 && categoryButtons[0] != NULL) {

			(*it)->x = categoryButtons[0]->width;
			(*it)->y = bHeader->height;
			(*it)->width = width - categoryButtons[0]->width;

			(*it)->setupPositions();
		}
	}

	// don't override user selection on resize
}


void OptionsScreen::render(int xm, int ym, float a) {

	renderBackground();

	super::render(xm, ym, a);

	int xmm = xm * width / minecraft->width;
	int ymm = ym * height / minecraft->height - 1;

	if (currentOptionPane != NULL)
		currentOptionPane->render(minecraft, xmm, ymm);
}

void OptionsScreen::removed() {
}

void OptionsScreen::closeOptions() {
	minecraft->options.save();
	if (minecraft->level != NULL) {
		minecraft->setScreen(new PauseScreen(false));
	} else {
		minecraft->screenChooser.setScreen(SCREEN_STARTMENU);
	}
}

bool OptionsScreen::handleBackEvent(bool isDown) {
	if (!isDown) {
		closeOptions();
	}
	return true;
}

void OptionsScreen::keyPressed(int eventKey) {
	if (eventKey == Keyboard::KEY_ESCAPE) {
		closeOptions();
		return;
	}
	Screen::keyPressed(eventKey);
}

void OptionsScreen::buttonClicked(Button* button) {

	if (button == btnClose) {
		closeOptions();
	}
	else if (button == btnChangeUsername) {
		minecraft->options.save();
		minecraft->setScreen(new UsernameScreen());
	}
	else if (button == btnCredits) {
		minecraft->options.save();
		minecraft->setScreen(new CreditsScreen());
	}
	else {
		// Match by identity, so adding a category cannot fall outside a range
		// check like the old `id >= 2 && id <= 4` did.
		for (size_t i = 0; i < categoryButtons.size(); ++i)
		{
			if (categoryButtons[i] == button)
			{
				selectCategory((int)i);
				break;
			}
		}
	}
}

void OptionsScreen::selectCategory(int index) {

	int currentIndex = 0;

	for (std::vector<Touch::TButton*>::iterator it = categoryButtons.begin(); it != categoryButtons.end(); ++it) {

		if (index == currentIndex)
			(*it)->selected = true;
		else
			(*it)->selected = false;

		currentIndex++;
	}

	if (index < (int)optionPanes.size())
		currentOptionPane = optionPanes[index];
}

void OptionsScreen::generateOptionScreens() {

	optionPanes.push_back(new OptionsPane());   // 0 Game
	optionPanes.push_back(new OptionsPane());   // 1 Controls
	optionPanes.push_back(new OptionsPane());   // 2 Graphics
	optionPanes.push_back(new OptionsPane());   // 3 Sound

	// Game Pane (index 0)
	//
	// GUI_SCALE is intentionally absent: it lives in the Graphics pane, where the
	// other display options are, and on SF2000 it only offers gentle 1.0x-1.5x
	// steps around the 1.2x default. See Minecraft::setSize.
	optionPanes[0]->createOptionsGroup("options.group.game")
		.addOptionItem(&Options::Option::DIFFICULTY, minecraft)
		.addOptionItem(&Options::Option::AUTO_JUMP, minecraft)
		.addOptionItem(&Options::Option::SERVER_VISIBLE, minecraft)
		.addOptionItem(&Options::Option::THIRD_PERSON, minecraft);

	// Controls Pane (index 1)
	optionPanes[1]->createOptionsGroup("options.group.controls")
		.addOptionItem(&Options::Option::SENSITIVITY, minecraft)
		.addOptionItem(&Options::Option::INVERT_MOUSE, minecraft);

	// Graphics Pane (index 2)
	optionPanes[2]->createOptionsGroup("options.group.graphics")
		.addOptionItem(&Options::Option::GRAPHICS, minecraft)
		.addOptionItem(&Options::Option::VIEW_BOBBING, minecraft)
		.addOptionItem(&Options::Option::AMBIENT_OCCLUSION, minecraft)
		.addOptionItem(&Options::Option::GUI_SCALE, minecraft)
		.addOptionItem(&Options::Option::RENDER_DISTANCE, minecraft)
		.addOptionItem(&Options::Option::BLOCK_RESOLUTION, minecraft)
		.addOptionItem(&Options::Option::LIMIT_FRAMERATE, minecraft);

	// Sound Pane (index 3)
	//
	// Music and sound used to sit at the bottom of the Graphics pane, which is
	// where you do not look for them. They get their own category.
	optionPanes[3]->createOptionsGroup("options.group.sound")
		.addOptionItem(&Options::Option::MUSIC, minecraft)
		.addOptionItem(&Options::Option::SOUND, minecraft);
}

void OptionsScreen::mouseClicked(int x, int y, int buttonNum) {

	if (currentOptionPane != NULL)
		currentOptionPane->mouseClicked(minecraft, x, y, buttonNum);

	super::mouseClicked(x, y, buttonNum);
}

void OptionsScreen::mouseReleased(int x, int y, int buttonNum) {

	if (currentOptionPane != NULL)
		currentOptionPane->mouseReleased(minecraft, x, y, buttonNum);

	super::mouseReleased(x, y, buttonNum);
}

void OptionsScreen::debugDumpOptionWidgets(int categoryIndex) const {
	if (categoryIndex < 0 || categoryIndex >= (int)optionPanes.size())
		return;

	OptionsPane* pane = optionPanes[categoryIndex];
	LOGI("[UI DUMP] pane %d rect = (%d,%d) %dx%d", categoryIndex, pane->x, pane->y, pane->width, pane->height);

	struct Dump {
		static void walk(GuiElement* e, int depth) {
			const char* kind = "element";
			if (dynamic_cast<Slider*>(e)) kind = "SLIDER";
			else if (dynamic_cast<OptionsItem*>(e)) kind = "item";
			else if (dynamic_cast<OptionsGroup*>(e)) kind = "group";
			LOGI("[UI DUMP] %*s%-7s rect=(%3d,%3d) %3dx%3d", depth * 2, "", kind,
			     e->x, e->y, e->width, e->height);
			GuiElementContainer* c = dynamic_cast<GuiElementContainer*>(e);
			if (!c) return;
			const std::vector<GuiElement*>& kids = c->getChildren();
			for (size_t i = 0; i < kids.size(); ++i)
				walk(kids[i], depth + 1);
		}
	};

	Dump::walk(pane, 0);
}

bool OptionsScreen::debugGetOptionRect(int categoryIndex, const Options::Option* option,
                                       int* outX, int* outY, int* outW, int* outH) const {
	if (outX) *outX = 0;
	if (outY) *outY = 0;
	if (outW) *outW = 0;
	if (outH) *outH = 0;

	if (categoryIndex < 0 || categoryIndex >= (int)optionPanes.size())
		return false;

	struct Find {
		static bool walk(GuiElement* e, const Options::Option* opt,
		                 int* rx, int* ry, int* rw, int* rh) {
			Slider* sl = dynamic_cast<Slider*>(e);
			OptionButton* ob = dynamic_cast<OptionButton*>(e);
			const bool isTarget =
				(sl && sl->getOption() == opt) ||
				(ob && ob->getOption() == opt);
			if (isTarget) {
				if (rx) *rx = e->x;
				if (ry) *ry = e->y;
				if (rw) *rw = e->width;
				if (rh) *rh = e->height;
				return true;
			}
			GuiElementContainer* c = dynamic_cast<GuiElementContainer*>(e);
			if (!c) return false;
			const std::vector<GuiElement*>& kids = c->getChildren();
			for (size_t i = 0; i < kids.size(); ++i)
				if (walk(kids[i], opt, rx, ry, rw, rh)) return true;
			return false;
		}
	};

	return Find::walk(optionPanes[categoryIndex], option, outX, outY, outW, outH);
}

bool OptionsScreen::debugIsPointOnOption(int categoryIndex, const Options::Option* option, int x, int y) const {
	int rx = 0, ry = 0, rw = 0, rh = 0;
	if (!debugGetOptionRect(categoryIndex, option, &rx, &ry, &rw, &rh))
		return false;
	return rx <= x && x < rx + rw && ry <= y && y < ry + rh;
}

/*
 * D-Pad focus for the options screen.
 *
 * The buttons (category tabs, close) come from the base implementation. The
 * actual settings are sliders and toggles inside the option pane tree, which is
 * not reachable from Screen, so they are collected here.
 */
void OptionsScreen::collectFocusTargets(std::vector<FocusTarget>& out)
{
	super::collectFocusTargets(out);

	if (currentOptionPane == NULL)
		return;

	struct Walk {
		static void go(GuiElement* e, std::vector<FocusTarget>& out) {
			Slider* s = dynamic_cast<Slider*>(e);
			if (s && s->visible)
			{
				out.push_back(FocusTarget(e->x, e->y, e->width, e->height, NULL, s->getOption()));
			}
			else
			{
				OptionButton* ob = dynamic_cast<OptionButton*>(e);
				if (ob && ob->visible)
					out.push_back(FocusTarget(e->x, e->y, e->width, e->height, NULL, ob->getOption()));
			}

			GuiElementContainer* c = dynamic_cast<GuiElementContainer*>(e);
			if (!c) return;
			const std::vector<GuiElement*>& kids = c->getChildren();
			for (size_t i = 0; i < kids.size(); ++i)
				go(kids[i], out);
		}
	};

	Walk::go(currentOptionPane, out);
}

bool OptionsScreen::adjustFocusedSlider(int dir)
{
	refreshFocus();
	if (currentOptionPane == NULL) return false;
	if (focusIndex < 0 || focusIndex >= (int)focusTargets.size()) return false;

	const Options::Option* opt = focusTargets[focusIndex].sliderOption;
	if (opt == NULL) return false;

	struct Nudge {
		static bool go(GuiElement* e, const Options::Option* o, Minecraft* mc, int dir) {
			Slider* s = dynamic_cast<Slider*>(e);
			if (s && s->getOption() == o) { s->nudge(mc, dir); return true; }
			GuiElementContainer* c = dynamic_cast<GuiElementContainer*>(e);
			if (!c) return false;
			const std::vector<GuiElement*>& kids = c->getChildren();
			for (size_t i = 0; i < kids.size(); ++i)
				if (go(kids[i], o, mc, dir)) return true;
			return false;
		}
	};

	return Nudge::go(currentOptionPane, opt, minecraft, dir);
}

void OptionsScreen::tick() {

	if (currentOptionPane != NULL)
		currentOptionPane->tick(minecraft);

	super::tick();
}