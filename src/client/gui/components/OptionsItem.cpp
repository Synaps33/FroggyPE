#include "OptionsItem.h"
#include "../../Minecraft.h"
#include "../../../util/Mth.h"
#include "../../../locale/I18n.h"
#include "../../../platform/input/Mouse.h"

OptionsItem::OptionsItem( std::string label, GuiElement* element, const Options::Option* option )
: GuiElementContainer(false, true, 0, 0, 24, 12),
  label(label),
  option(option) {
	  addChild(element);
}

void OptionsItem::setupPositions() {
	int currentHeight = 0;
	for(std::vector<GuiElement*>::iterator it = children.begin(); it != children.end(); ++it) {
		(*it)->x = x + width - (*it)->width - 15;
		(*it)->y = y + currentHeight;
		currentHeight += (*it)->height;
	}
	height = currentHeight;
}

void OptionsItem::render( Minecraft* minecraft, int xm, int ym ) {
	int yOffset = (height - 8) / 2;
	std::string displayLabel = label;
	if (option == &Options::Option::RENDER_DISTANCE) {
		int val = minecraft->options.viewDistance;
		if (val >= 0 && val < 6) {
			static const char* s_names[] = { "Far", "Normal", "Short", "Tiny", "Very Tiny", "Minimal" };
			displayLabel += ": ";
			displayLabel += s_names[val];
		}
	} else if (option == &Options::Option::BLOCK_RESOLUTION) {
		int val = minecraft->options.blockResolution;
		static const char* s_resNames[] = { "1/4", "2/4", "3/4", "4/4" };
		displayLabel = "Resolution: ";
		if (val >= 0 && val <= 3) displayLabel += s_resNames[val];
		else displayLabel += "2/4";
	} else if (option == &Options::Option::DIFFICULTY) {
		int val = minecraft->options.difficulty;
		if (val >= 0 && val < 4) {
			displayLabel += ": " + I18n::get(Options::DIFFICULTY_NAMES[val]);
		}
	}
	minecraft->font->draw(displayLabel, (float)x, (float)y + yOffset, 0x909090, false);
	super::render(minecraft, xm, ym);
}

void OptionsItem::mouseClicked( Minecraft* minecraft, int x, int y, int buttonNum ) {
	super::mouseClicked(minecraft, x, y, buttonNum);
	if (buttonNum == MouseAction::ACTION_LEFT && pointInside(x, y)) {
		bool hitChild = false;
		for (size_t i = 0; i < children.size(); ++i) {
			if (children[i]->pointInside(x, y)) {
				hitChild = true;
				break;
			}
		}
		if (!hitChild && option != NULL) {
			if (option == &Options::Option::RENDER_DISTANCE) {
				// Cycle: Minimal(5) -> Very Tiny(4) -> Tiny(3) -> Short(2) -> Normal(1) -> Far(0) -> Minimal(5)
				int cur = minecraft->options.viewDistance;
				int next;
				if (cur == 5) next = 4;
				else if (cur == 4) next = 3;
				else if (cur == 3) next = 2;
				else if (cur == 2) next = 1;
				else if (cur == 1) next = 0;
				else next = 5;
				minecraft->options.set(option, next);
			} else if (option == &Options::Option::BLOCK_RESOLUTION) {
				int next = (minecraft->options.blockResolution + 1) & 3;
				minecraft->options.set(option, next);
			} else if (option == &Options::Option::DIFFICULTY) {
				int next = (minecraft->options.difficulty + 1) & 3;
				minecraft->options.set(option, next);
			}
		}
	}
}