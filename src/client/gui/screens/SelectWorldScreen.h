#ifndef NET_MINECRAFT_CLIENT_GUI_SCREENS__SelectWorldScreen_H__
#define NET_MINECRAFT_CLIENT_GUI_SCREENS__SelectWorldScreen_H__

#include "../Screen.h"
#include "../TweenData.h"
#include "../components/Button.h"
#include "../components/SmallButton.h"
#include "../components/RolledSelectionListH.h"
#include "../../Minecraft.h"
#include "../../../world/level/storage/LevelStorageSource.h"


class SelectWorldScreen;

/*
 * FocusTarget::id marker for the world carousel focus stop. Distinct from the
 * slot ids used by the inventory grid, and negative so it cannot collide with a
 * slot index.
 */
static const int kWorldCarouselFocus = -2;

//
// Scrolling World selection list
//
class WorldSelectionList : public RolledSelectionListH
{
public:
	WorldSelectionList(Minecraft* _minecraft, int _width, int _height);
	virtual void tick();
	void stepLeft();
	void stepRight();

	void commit();

	// D-Pad support: the pad steps through worlds instead of dragging.
	void moveSelection(int dir);
	int  selectedIndex() const { return selectedItem; }
protected:
	virtual int getNumberOfItems();
	virtual void selectItem(int item, bool doubleClick);
	virtual bool isSelectedItem(int item);

	virtual void renderBackground() {}
	virtual void renderItem(int i, int x, int y, int h, Tesselator& t);
	virtual float getPos(float alpha);
	virtual void touched() { mode = 0; }
	virtual bool capXPosition();
private:
	TweenData td;
	void tweenInited();

	int selectedItem;
	int _height;
	LevelSummaryList levels;
	std::vector<StringVector> _descriptions;
	StringVector _imageNames;

	bool hasPickedLevel;
	LevelSummary pickedLevel;

	int stoppedTick;
	int currentTick;
	float accRatio;
	int mode;
	
	friend class SelectWorldScreen;
};

//
// Delete World screen
//
#include "ConfirmScreen.h"
class DeleteWorldScreen: public ConfirmScreen
{
public:
	DeleteWorldScreen(const LevelSummary& levelId);
protected:
	virtual void postResult(bool isOk);
private:
	LevelSummary _level;
};


//
// Select world screen
//
class SelectWorldScreen: public Screen
{
	typedef Screen super;
public:
	SelectWorldScreen();
	virtual ~SelectWorldScreen();

	virtual void init();
	virtual void setupPositions();
	virtual void tick();

	virtual bool isIndexValid(int index);
	virtual bool handleBackEvent(bool isDown);
	virtual void buttonClicked(Button* button);
	virtual void keyPressed(int eventKey);
	virtual void mouseClicked(int x, int y, int buttonNum);

	// The world carousel is a focus stop of its own, and Left/Right step worlds
	// rather than moving the focus.
	virtual int  getNavMode() const { return GUI_NAV_FOCUS; }
	virtual void collectFocusTargets(std::vector<FocusTarget>& out);
	virtual bool focusDirection(int dx, int dy);
	virtual void focusMoved();

	// Diagnostic hooks used by the automated tests.
	int  focusWorldIndex() const;
	void setPadFocusStep(int dx, int dy);

	void render(int xm, int ym, float a);

	bool isInGameScreen();
private:
	void loadLevelSource();
	std::string getUniqueLevelName(const std::string& level);

	Button bDelete;
	Button bCreate;
	Button bBack;
	Button bWorldView;
	WorldSelectionList* worldsList;
	LevelSummaryList levels;

	bool _mouseHasBeenUp;
	bool _hasStartedLevel;
	//LevelStorageSource* levels;
};

#endif /*NET_MINECRAFT_CLIENT_GUI_SCREENS__SelectWorldScreen_H__*/
