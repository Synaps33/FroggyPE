#ifndef NET_MINECRAFT_CLIENT_GUI_SCREENS__OptionsScreen_H__
#define NET_MINECRAFT_CLIENT_GUI_SCREENS__OptionsScreen_H__

#include "../Screen.h"
#include "../components/Button.h"

class ImageButton;
class OptionsPane;

class OptionsScreen: public Screen
{
	typedef Screen super;

	void init();
	void generateOptionScreens();

public:
	OptionsScreen();
	~OptionsScreen();

	void setupPositions();
	void buttonClicked(Button* button);
	void render(int xm, int ym, float a);
	void removed();
	void selectCategory(int index);

	virtual void mouseClicked(int x, int y, int buttonNum);
	virtual void mouseReleased(int x, int y, int buttonNum);
	virtual void tick();
	virtual bool handleBackEvent(bool isDown);
	virtual void keyPressed(int eventKey);
	void closeOptions();

	// Diagnostic hook used by the automated UI tests: reports the on-screen
	// rectangles of every option widget so hit areas can be verified.
	void debugDumpOptionWidgets(int categoryIndex) const;

	// D-Pad focus: the option controls live in the pane tree, not in buttons.
	virtual void collectFocusTargets(std::vector<FocusTarget>& out);
	virtual bool adjustFocusedSlider(int dir);

	// Diagnostic hook used by the automated UI tests: is a point, in the GUI's
	// logical coordinates, inside the control bound to this option? Lets a
	// legitimate tap on a control be told apart from a stray tap.
	bool debugIsPointOnOption(int categoryIndex, const Options::Option* option, int x, int y) const;

	// Diagnostic hook used by the automated UI tests: the on-screen rectangle of
	// the control bound to an option, in logical coordinates. Tests derive their
	// tap points from this instead of hard-coded pixels, so they survive a change
	// of GUI scale or a layout tweak.
	bool debugGetOptionRect(int categoryIndex, const Options::Option* option,
	                        int* outX, int* outY, int* outW, int* outH) const;

private:
	Touch::THeader* bHeader;
	ImageButton* btnClose;

	Button* btnChangeUsername;
	Button* btnCredits;   // <-- ADD THIS

	std::vector<Touch::TButton*> categoryButtons;
	std::vector<OptionsPane*> optionPanes;

	OptionsPane* currentOptionPane;

	int selectedCategory;
};

#endif /*NET_MINECRAFT_CLIENT_GUI_SCREENS__OptionsScreen_H__*/