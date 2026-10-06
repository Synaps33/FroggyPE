#ifndef NET_MINECRAFT_CLIENT_GUI_COMPONENTS__Slider_H__
#define NET_MINECRAFT_CLIENT_GUI_COMPONENTS__Slider_H__

#include "GuiElement.h"
#include "../../../client/Options.h"
enum SliderType {
	SliderProgress, // Sets slider between {0..1}
	SliderStep // Uses the closest step
};
class Slider : public GuiElement {
	typedef GuiElement super;
public:
	// Creates a progress slider with no steps
	Slider(Minecraft* minecraft, const Options::Option* option, float progressMin, float progressMax);
	Slider(Minecraft* minecraft, const Options::Option* option, const std::vector<int>& stepVec);
	virtual void render( Minecraft* minecraft, int xm, int ym );

	virtual void mouseClicked( Minecraft* minecraft, int x, int y, int buttonNum );

	virtual void mouseReleased( Minecraft* minecraft, int x, int y, int buttonNum );

	virtual void tick(Minecraft* minecraft);

	// Which option this slider edits. Used by the automated UI tests to tell a
	// real control apart from stray taps on the background.
	const Options::Option* getOption() const { return option; }

	/*
	 * Step the value by one, without any pointer interaction. This is what the
	 * D-Pad uses when focus is on a value control: Left/Right should change the
	 * setting, not require a click on the track first.
	 */
	void nudge(Minecraft* minecraft, int dir);
	
private:
	virtual void setOption(Minecraft* minecraft);
	void captureTrack();
	void updateStepFromX(int xPos);
	void updateProgressFromX(int xPos);

private:
	SliderType sliderType;
	std::vector<int> sliderSteps;
	bool mouseDownOnElement;
	float percentage;
	int curStepValue;
	int curStep;
	int numSteps;
	float progressMin;
	float progressMax;
	const Options::Option* option;

	// Track geometry captured when the drag starts. Changing an option can
	// resize and re-lay-out the whole screen mid-gesture (the GUI Scale slider
	// does exactly that), so the track must not be re-read from x/width later.
	int dragTrackOrigin;
	int dragTrackWidth;
};

#endif /*NET_MINECRAFT_CLIENT_GUI_COMPONENTS__Slider_H__*/
