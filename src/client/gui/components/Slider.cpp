#include "Slider.h"
#include "../../Minecraft.h"
#include "../../renderer/Textures.h"
#include "../Screen.h"
#include "../../../util/Mth.h"
#include <algorithm>
#include <assert.h>

Slider::Slider(Minecraft* minecraft, const Options::Option* option,  float progressMin, float progressMax)
: sliderType(SliderProgress), mouseDownOnElement(false), option(option), numSteps(0), progressMin(progressMin), progressMax(progressMax), dragTrackOrigin(0), dragTrackWidth(1) {
	if(option != NULL) {
		percentage = (minecraft->options.getProgressValue(option) - progressMin) / (progressMax - progressMin);
	}
}

Slider::Slider(Minecraft* minecraft, const Options::Option* option, const std::vector<int>& stepVec )
: sliderType(SliderStep),
  curStepValue(0),
  curStep(0),
  sliderSteps(stepVec),
  mouseDownOnElement(false),
  option(option),
  percentage(0),
  progressMin(0.0f),
  progressMax(1.0),
  dragTrackOrigin(0),
  dragTrackWidth(1) {
	assert(stepVec.size() > 1);
	numSteps = sliderSteps.size();
	if(option != NULL) {
		// initialize slider position based on the current option value
		curStepValue = minecraft->options.getIntValue(option);
		auto currentItem = std::find(sliderSteps.begin(), sliderSteps.end(), curStepValue);
		if(currentItem != sliderSteps.end()) {
			curStep = static_cast<int>(currentItem - sliderSteps.begin());
		} else {
			// fallback to first step
			curStep = 0;
			curStepValue = sliderSteps[0];
		}
		percentage = float(curStep) / float(numSteps - 1);
	}
}

void Slider::render( Minecraft* minecraft, int xm, int ym ) {
	if(sliderType == SliderStep && !mouseDownOnElement && option != NULL) {
		int currentVal = minecraft->options.getIntValue(option);
		if(currentVal != curStepValue) {
			curStepValue = currentVal;
			auto currentItem = std::find(sliderSteps.begin(), sliderSteps.end(), curStepValue);
			if(currentItem != sliderSteps.end()) {
				curStep = static_cast<int>(currentItem - sliderSteps.begin());
			} else {
				curStep = 0;
			}
			percentage = float(curStep) / (numSteps - 1);
		}
	}

	int xSliderStart = x + 5;
	int xSliderEnd = x + width - 5;
	int ySliderStart = y + 6;
	int ySliderEnd = y + 9;
	int handleSizeX = 9;
	int handleSizeY = 15;
	int barWidth = xSliderEnd - xSliderStart;
	fill(xSliderStart, ySliderStart, xSliderEnd, ySliderEnd, 0xff606060);
	if(sliderType == SliderStep) {
		if(numSteps > 1) {
			int stepDistance = barWidth / (numSteps - 1);
			for(int a = 0; a <= numSteps - 1; ++a) {
				int renderSliderStepPosX = xSliderStart + a * stepDistance + 1;
				fill(renderSliderStepPosX - 1, ySliderStart - 2, renderSliderStepPosX + 1, ySliderEnd + 2, 0xff606060);
			}
		}
	}
	minecraft->textures->loadAndBindTexture("gui/touchgui.png");
	blit(xSliderStart + (int)(percentage * barWidth) - handleSizeX / 2, y, 226, 126, handleSizeX, handleSizeY, handleSizeX, handleSizeY);
}

/*
 * Captures the track geometry at the moment the drag starts.
 *
 * Writing an option can resize and re-lay-out the entire screen while the
 * gesture is still in progress -- the GUI Scale slider does precisely that,
 * because Minecraft::optionUpdated() calls setSize(). Reading x/width again on
 * release would then use a completely different rectangle and produce a
 * nonsensical step, which is what used to fling the UI to 400%.
 */
void Slider::captureTrack()
{
	dragTrackOrigin = x + 5;
	dragTrackWidth = width - 10;
	if (dragTrackWidth <= 0) dragTrackWidth = 1;
}

/* Maps a track coordinate onto a step, snapping to the nearest one. */
void Slider::updateStepFromX( int xPos )
{
	if(numSteps <= 1 || sliderSteps.empty()) {
		curStep = 0;
		curStepValue = sliderSteps.empty() ? 0 : sliderSteps[0];
		return;
	}

	percentage = float(xPos - dragTrackOrigin) / float(dragTrackWidth);
	percentage = Mth::clamp(percentage, 0.0f, 1.0f);

	curStep = Mth::floor(percentage * float(numSteps - 1) + 0.5f);
	if (curStep < 0) curStep = 0;
	if (curStep >= numSteps) curStep = numSteps - 1;

	curStepValue = sliderSteps[curStep];
	percentage = float(curStep) / float(numSteps - 1);
}

void Slider::updateProgressFromX( int xPos )
{
	percentage = float(xPos - dragTrackOrigin) / float(dragTrackWidth);
	percentage = Mth::clamp(percentage, 0.0f, 1.0f);
}

void Slider::mouseClicked( Minecraft* minecraft, int xPos, int y, int buttonNum ) {
	if (buttonNum != MouseAction::ACTION_LEFT)
		return;
	if (!pointInside(xPos, y))
		return;

	mouseDownOnElement = true;
	captureTrack();

	if (sliderType == SliderStep)
		updateStepFromX(xPos);
	else
		updateProgressFromX(xPos);

	setOption(minecraft);
}

void Slider::mouseReleased( Minecraft* minecraft, int xPos, int y, int buttonNum ) {
	/*
	 * Only react when this slider is genuinely being dragged.
	 *
	 * Without this guard every left-button release, anywhere on the screen, was
	 * rewriting every step slider from the cursor position. That is what made the
	 * whole UI jump to 400% after touching anything in the options screen.
	 */
	if (!mouseDownOnElement)
		return;

	mouseDownOnElement = false;

	if (sliderType == SliderStep) {
		updateStepFromX(xPos);
		setOption(minecraft);
	}
}

void Slider::tick(Minecraft* minecraft) {
	if (!mouseDownOnElement || minecraft->screen == NULL)
		return;

	int xm = Mouse::getX();
	int ym = Mouse::getY();
	minecraft->screen->toGUICoordinate(xm, ym);

	if (sliderType == SliderStep)
		updateStepFromX(xm);
	else
		updateProgressFromX(xm);

	setOption(minecraft);
}

void Slider::nudge(Minecraft* minecraft, int dir)
{
	if (sliderType != SliderStep || numSteps <= 1)
		return;

	// Re-read the current value first: it can be changed from elsewhere (a
	// preset, another screen) while this widget is not focused.
	if (option != NULL)
	{
		const int current = minecraft->options.getIntValue(option);
		if (current != curStepValue)
		{
			curStepValue = current;
			std::vector<int>::const_iterator it =
				std::find(sliderSteps.begin(), sliderSteps.end(), curStepValue);
			curStep = (it != sliderSteps.end()) ? (int)(it - sliderSteps.begin()) : 0;
		}
	}

	int next = curStep + (dir < 0 ? -1 : 1);
	if (next < 0) next = 0;
	if (next >= numSteps) next = numSteps - 1;
	if (next == curStep)
		return;

	curStep = next;
	curStepValue = sliderSteps[curStep];
	percentage = float(curStep) / float(numSteps - 1);
	setOption(minecraft);
}

void Slider::setOption( Minecraft* minecraft ) {
	if(option != NULL) {
		if(sliderType == SliderStep) {
			if(minecraft->options.getIntValue(option) != curStepValue) {
				minecraft->options.set(option, curStepValue);
			}
		} else {
			float v = percentage * (progressMax - progressMin) + progressMin;
			if(minecraft->options.getProgressValue(option) != v) {
				minecraft->options.set(option, v);
			}
		}
	}
}
