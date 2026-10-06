#include "IngameBlockSelectionScreen.h"
#include "../../renderer/TileRenderer.h"
#include "../../player/LocalPlayer.h"
#include "../../renderer/gles.h"
#include "../../Minecraft.h"
#include "../../sound/SoundEngine.h"
#include "../../../world/entity/player/Inventory.h"
#include "../../../platform/input/Mouse.h"

#include "../Gui.h"
#include "../../renderer/Textures.h"
#include "../../gamemode/GameMode.h"
#include "ArmorScreen.h"
#include "../../../world/item/crafting/Recipe.h"
#include "../components/Button.h"

#if defined(__APPLE__)
    static const std::string demoVersionString("Not available in the Lite version");
#else
    static const std::string demoVersionString("Not available in the demo version");
#endif

IngameBlockSelectionScreen::IngameBlockSelectionScreen() 
:	selectedItem(0),
	_area(0,0,0,0),
	_pendingQuit(false),
	InventoryRows(1),
	InventoryCols(1),
	InventorySize(1),
	bCraft(2, "Crafting"),
	bArmor(1, "Armor")
{
}

void IngameBlockSelectionScreen::init()
{
	Inventory* inventory = minecraft->player->inventory;
	InventoryCols = minecraft->isCreativeMode()? 13 : 9;
	InventorySize = inventory->getContainerSize() - Inventory::MAX_SELECTION_SIZE;
	InventoryRows = 1 + (InventorySize - 1) / InventoryCols;

	_area = RectangleArea(	(float)getSlotPosX(0) - 4,
							(float)getSlotPosY(0) - 4,
							(float)getSlotPosX(InventoryCols) + 4, 
							(float)getSlotPosY(InventoryRows) + 4);

	/*
	 * The side buttons are created first, before anything that can bail out.
	 * This function used to return early when the selected hotbar slot was
	 * empty, which happened before the buttons were added, so an empty inventory
	 * produced a screen with no Armor and no Crafting button at all.
	 */
	if (!minecraft->isCreativeMode()) {
		bArmor.width = 42;
		bArmor.x = 0;
		bArmor.y = height - bArmor.height;
		buttons.push_back(&bArmor);

		// Crafting sits directly above Armor so both stay reachable with the
		// discrete D-Pad cursor without overlapping the slot grid.
		bCraft.width = 42;
		bCraft.x = 0;
		bCraft.y = bArmor.y - bCraft.height;
		buttons.push_back(&bCraft);
	}

	ItemInstance* selected = inventory->getSelected();
	if (!selected || selected->isNull()) {
		selectedItem = 0;
		return;
	}

	for (int i = Inventory::MAX_SELECTION_SIZE; i < InventorySize; i++) {
		if (selected == minecraft->player->inventory->getItem(i))
		{
			selectedItem = i - Inventory::MAX_SELECTION_SIZE;
			break;
		}
	}
	if (!isAllowed(selectedItem))
		selectedItem = 0;
}

void IngameBlockSelectionScreen::removed()
{
	minecraft->gui.inventoryUpdated();
}

void IngameBlockSelectionScreen::renderSlots()
{
	//static Stopwatch w;
	//w.start();

	glColor4f2(1, 1, 1, 1);

	blitOffset = -90;

	//glEnable2(GL_RESCALE_NORMAL);
	//glPushMatrix2();
	//glRotatef2(180, 1, 0, 0);
	//Lighting::turnOn();
	//glPopMatrix2();

	minecraft->textures->loadAndBindTexture("gui/gui.png");
	for (int r = 0; r < InventoryRows; r++)
	{
		int x = getSlotPosX(0) - 3;
		int y = getSlotPosY(r) - 3;

		if (InventoryCols == 9) {
			blit(x, y, 0, 0, 182, 22);
		} else {
			// first 8 slots
			blit(x, y, 0, 0, 182-20, 22);
			// last k slots
			const int k = 5;
			const int w = k * 20;
			blit(x + 162, y, 182-w, 0, w, 22);
		}
	}
	if (selectedItem >= 0)
	{
		int x = getSlotPosX(selectedItem % InventoryCols) - 4;// width / 2 - 182 / 2 - 1 + () * 20;
		int y = getSlotPosY(selectedItem / InventoryCols) - 4;// height - 22 * 3 - 1 - (selectedItem / InventoryCols) * 22;
		blit(x, y, 0, 22, 24, 22);
	}

	for (int r = 0; r < InventoryRows; r++)
	{
		int y = getSlotPosY(r);
		for (int i = 0; i < InventoryCols; i++) {
			int x = getSlotPosX(i);
			renderSlot(r * InventoryCols + i + Inventory::MAX_SELECTION_SIZE, x, y, 0);
		}
	}

	//w.stop();
	//w.printEvery(1000, "render-blocksel");

	//glDisable2(GL_RESCALE_NORMAL);
	//Lighting::turnOn();
}

/*
 * Every visible slot becomes a focus stop, so the D-Pad walks the grid directly
 * instead of the player having to land a pointer on a 20 px cell. The Armor and
 * Crafting buttons come from the base implementation and are reachable by
 * moving off the left edge of the leftmost column.
 */
void IngameBlockSelectionScreen::collectFocusTargets(std::vector<FocusTarget>& out)
{
	super::collectFocusTargets(out);

	const int sh = getSlotHeight();
	for (int r = 0; r < InventoryRows; ++r)
	{
		for (int c = 0; c < InventoryCols; ++c)
		{
			const int slot = r * InventoryCols + c;
			if (!isAllowed(slot))
				continue;
			out.push_back(FocusTarget(getSlotPosX(c), getSlotPosY(r), 20, sh));
			out.back().id = slot;
		}
	}
}

/* Keep the grid's own selection in step with the focus. */
void IngameBlockSelectionScreen::focusMoved()
{
	const std::vector<FocusTarget>& ft = getFocusTargets();
	const int fi = getFocusIndex();
	if (fi < 0 || fi >= (int)ft.size())
		return;
	if (ft[fi].id >= 0 && isAllowed(ft[fi].id))
		selectedItem = ft[fi].id;
}

int IngameBlockSelectionScreen::getSlotPosX(int slotX) {
	return width / 2 - InventoryCols * 10 + slotX * 20 + 2;
}

int IngameBlockSelectionScreen::getSlotPosY(int slotY) {
	//return height - 63 - 22 * (3 - slotY);
	int yy = InventoryCols==9? 8 : 3;
	return yy + slotY * getSlotHeight();
}

//int IngameBlockSelectionScreen::getLinearSlotId(int x, int y) {
//	return 
//}


#include "../../../world/item/ItemInstance.h"
#include "../../renderer/entity/ItemRenderer.h"

void IngameBlockSelectionScreen::renderSlot(int slot, int x, int y, float a)
{
	ItemInstance* item = minecraft->player->inventory->getItem(slot);
	if (!item) return;

	ItemRenderer::renderGuiItem(minecraft->font, minecraft->textures, item, (float)x, (float)y, true);

	if (minecraft->gameMode->isCreativeType()) return;
	if (!isAllowed(slot - Inventory::MAX_SELECTION_SIZE)) return;

	glPushMatrix2();
	glScalef2(Gui::InvGuiScale + Gui::InvGuiScale, Gui::InvGuiScale + Gui::InvGuiScale, 1);
	const float k = 0.5f * Gui::GuiScale;
	minecraft->gui.renderSlotText(item, k*x, k*y, true, true);
	glPopMatrix2();
}

void IngameBlockSelectionScreen::keyPressed(int eventKey)
{
	int selX = selectedItem % InventoryCols;
	int selY = selectedItem / InventoryCols;

	int tmpSelectedSlot = selectedItem;

	Options& o = minecraft->options;
	if (eventKey == o.keyLeft.key && selX > 0)
	{
		tmpSelectedSlot -= 1;
	}
	else if (eventKey == o.keyRight.key && selX < (InventoryCols - 1))
	{
		tmpSelectedSlot += 1;
	}
	else if (eventKey == o.keyDown.key && selY < (InventoryRows - 1))
	{
		tmpSelectedSlot += InventoryCols;
	}
	else if (eventKey == o.keyUp.key && selY > 0)
	{
		tmpSelectedSlot -= InventoryCols;
	}

	if (isAllowed(tmpSelectedSlot))
		selectedItem = tmpSelectedSlot;

	if (eventKey == o.keyMenuOk.key)
		selectSlotAndClose();

#ifdef RPI
	if (eventKey == o.keyMenuCancel.key
		||	eventKey == Keyboard::KEY_ESCAPE)
		minecraft->setScreen(NULL);
#else
	if (eventKey == o.keyMenuCancel.key)
		minecraft->setScreen(NULL);
#endif
}

int IngameBlockSelectionScreen::getSelectedSlot(int x, int y)
{
	int left = width / 2 - InventoryCols * 10;
	int top = -4 + getSlotPosY(0);

	if (x >= left && y >= top)
	{
		int xSlot = (x - left) / 20;
		if (xSlot < InventoryCols) {
			int row = ((y-top) / getSlotHeight());
			return row * InventoryCols + xSlot;
		}
	}
	return -1;
}

void IngameBlockSelectionScreen::mouseClicked(int x, int y, int buttonNum)
{
	if (buttonNum == MouseAction::ACTION_LEFT) {

		int slot = getSelectedSlot(x, y);
		if (isAllowed(slot))
		{
			selectedItem = slot;
			//minecraft->soundEngine->playUI("random.click", 1, 1);
		} else {
			_pendingQuit = !_area.isInside((float)x, (float)y)
			            && !bArmor.isInside(x, y)
			            && !bCraft.isInside(x, y);
		}
	}
	if (!_pendingQuit)
		super::mouseClicked(x, y, buttonNum);
}

void IngameBlockSelectionScreen::mouseReleased(int x, int y, int buttonNum)
{
	if (buttonNum == MouseAction::ACTION_LEFT) {

		int slot = getSelectedSlot(x, y);
		if (isAllowed(slot) && slot == selectedItem)
		{
			selectSlotAndClose();
		} else {
			if (_pendingQuit && !_area.isInside((float)x, (float)y))
				minecraft->setScreen(NULL);
		}
	}
	if (!_pendingQuit)
		super::mouseReleased(x, y, buttonNum);
}

void IngameBlockSelectionScreen::selectSlotAndClose()
{
	Inventory* inventory = minecraft->player->inventory;
	// Flash the selected gui item
	//inventory->moveToSelectedSlot(selectedItem + Inventory::MAX_SELECTION_SIZE, true);
	inventory->moveToSelectionSlot(0, selectedItem + Inventory::MAX_SELECTION_SIZE, true);
	inventory->selectSlot(0);
	minecraft->gui.flashSlot(inventory->selected);

	minecraft->soundEngine->playUI("random.click", 1, 1);
	minecraft->setScreen(NULL);
}

void IngameBlockSelectionScreen::render( int xm, int ym, float a )
{
	glDisable2(GL_DEPTH_TEST);
	fill(0, 0, width, height, (0x80) << 24);
	glEnable2(GL_BLEND);

	glDisable2(GL_ALPHA_TEST);
	glEnable2(GL_BLEND);
	glBlendFunc2(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	renderSlots();
	renderDemoOverlay();

	glEnable2(GL_ALPHA_TEST);
	glDisable2(GL_BLEND);

	glEnable2(GL_DEPTH_TEST);

	Screen::render(xm, ym, a);
}

void IngameBlockSelectionScreen::renderDemoOverlay() {
#ifdef DEMO_MODE
	fill(	getSlotPosX(0) - 3,							getSlotPosY(3) - 3,
			getSlotPosX(InventoryCols) - 3, getSlotPosY(InventoryRows) - 3, 0xa0 << 24);

	const int centerX = (getSlotPosX(4) + getSlotPosX(5)) / 2;
	const int centerY = (getSlotPosY(3) + getSlotPosY(InventoryRows-1)) / 2 + 5;
	drawCenteredString(minecraft->font, demoVersionString, centerX, centerY, 0xffffffff);
#endif /*DEMO_MODE*/
}

bool IngameBlockSelectionScreen::isAllowed(int slot) {
	if (slot < 0 || slot >= InventorySize)
		return false;

	#ifdef DEMO_MODE
		return slot < (minecraft->isCreativeMode()? 28 : 27);
	#endif /*DEMO_MODE*/

	return true;
}

int IngameBlockSelectionScreen::getSlotHeight() {
	return InventoryCols==9? 22 : 20;
}

void IngameBlockSelectionScreen::buttonClicked( Button* button )
{
	if (button == &bArmor) {
		minecraft->setScreen(new ArmorScreen());
	}
	else if (button == &bCraft) {
		// Same target as LocalPlayer::startCrafting, but called directly: the
		// keyboard route spans a frame and was unreliable at this frame rate.
		// startCrafting still applies the creative-mode check itself.
		if (minecraft->player != NULL)
			minecraft->player->startCrafting((int)minecraft->player->x, (int)minecraft->player->y,
			                                  (int)minecraft->player->z, Recipe::SIZE_2X2);
	}
	super::buttonClicked(button);
}
