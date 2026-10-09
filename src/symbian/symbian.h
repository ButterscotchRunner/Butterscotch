#include <aknapp.h>
#include <akndoc.h>
#include <aknappui.h>
#include <aknwseventobserver.h>

class ButterscotchDocument: public CAknDocument {
public:
	static ButterscotchDocument* NewL(CEikApplication& aApp);
	virtual ~ButterscotchDocument();
protected:
	void ConstructL();
public:
	ButterscotchDocument(CEikApplication& aApp);
private:
	CEikAppUi* CreateAppUiL();
};

class ButterscotchApp: public CAknApplication {
private:
	CApaDocument* CreateDocumentL();
	TUid AppDllUid() const;
};

class ButterscotchContainer : public CCoeControl, MAknWsEventObserver {
public:
	CAknAppUi* iAppUi;
	CPeriodic* iPeriodic;

	void RestartTimerL(TInt aInterval);
	void ConstructL(const TRect& aRect, CAknAppUi* aAppUi);
	void HandleWsEventL(const TWsEvent &aEvent, CCoeControl *aDestination);
	void HandleResourceChange(TInt aType);
};

class ButterscotchAppUi : public CAknAppUi {
	ButterscotchContainer* iContainer;
public:
	void ConstructL();
	void HandleForegroundEventL(TBool aForeground);

	~ButterscotchAppUi();
	void HandleCommandL(TInt aCommand);
};

enum State {
	EStateInit,
	EStateTick,
	EStatePreExit,
	EStateExit,
};

struct KeyEvent {
	int type, value;
};
