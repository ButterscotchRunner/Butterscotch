#include <e32base.h>
#include <e32keys.h>
#include <coemain.h>
#include <w32std.h>
#include <aknapp.h>
#include <akndoc.h>
#include <aknappui.h>
#include <coecntrl.h>
#include <eikstart.h>
#include <aknwseventobserver.h>
#include <egl/egl.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

extern "C" {
#include "loop.h"
#include "log.h"
#include "gettime.h"
}

static EGLDisplay eglDisplay;
static EGLContext eglContext;
static EGLSurface eglSurface;
static EGLConfig eglConfig;

static Runner *g_runner;
static bool quit;
static bool shouldExit;
static bool foreground = true;
static int state = 0;

CommandLineArgs args;

#define LOG_BUFFER_SIZE 1024

extern "C" void platformLog(const logType type, const char *format, va_list va) {
    const char* textPrefix = "";
    char buffer[LOG_BUFFER_SIZE];
    
    switch (type) {
        case LOG_TYPE_NORMAL:
            break;
        case LOG_TYPE_WARNING:
            textPrefix = "Warning: ";
            break;
        case LOG_TYPE_ERROR:
            textPrefix = "Error: ";
            break;
        case LOG_TYPE_DEBUG:
            textPrefix = "Debug: ";
            break;
    }
    int written = snprintf(buffer, sizeof(buffer), "%s", textPrefix);
    
    if (written >= 0 && written < (int)sizeof(buffer)) {
        vsnprintf(buffer + written, (int)sizeof(buffer) - written, format, va);
    }
    buffer[sizeof(buffer) - 1] = '\0';
    
    RDebug::Printf("%s", buffer);
//    puts(buffer);
}

extern "C" uint64_t nowNanos(void) {
	TTime time;
	time.HomeTime();
	return (uint64_t)(time.Int64() * 1000);
}

class ButterscotchContainer : public CCoeControl, MAknWsEventObserver {
public:
	CAknAppUi* iAppUi;
	CPeriodic* iPeriodic;
	static TInt LoopCallBack(TAny* p) {
		ButterscotchContainer* container = (ButterscotchContainer*) p;
		if (quit) {
			container->iAppUi->Exit();
			return EFalse;
		}
		
		if (state == 0) {
			if (loop_init(args, nullptr) != -1) {
				quit = true;
			}
			state = 1;
			return ETrue;
		} else if (state == 1) {
			if (loop_step() != -1) {
				state = 2;
			}
			return ETrue;
		} else if (state == 2) {
			if (loop_exit() != -1) {
				quit = true;
			}
			state = 0;
			return ETrue;
		}

		return ETrue;
	}

	void RestartTimerL(TInt aInterval) {
		if (iPeriodic) iPeriodic->Cancel();
		else iPeriodic = CPeriodic::NewL(CActive::EPriorityLow);
		iPeriodic->Start(aInterval, aInterval, TCallBack(ButterscotchContainer::LoopCallBack, this));
	}
	
	void ConstructL(const TRect& aRect, CAknAppUi* aAppUi) {
		iAppUi = aAppUi;
		
		CAknWsEventMonitor* monitor = iAppUi->EventMonitor();
		monitor->AddObserverL(this);
		monitor->Enable();
		
		CreateWindowL();
		iAppUi->SetOrientationL(CAknAppUiBase::EAppUiOrientationLandscape);
		SetExtentToWholeScreen();

		SetFocus(ETrue);

//		Window().EnableAdvancedPointers();
		EnableDragEvents();
		ActivateL();

		EGLint attribs[] = {
			EGL_BUFFER_SIZE,       16,
			EGL_DEPTH_SIZE,        16,
			EGL_STENCIL_SIZE,      0,
			EGL_SURFACE_TYPE,      EGL_WINDOW_BIT,
			EGL_SAMPLES, 0,
			EGL_COLOR_BUFFER_TYPE, EGL_RGB_BUFFER,
			EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
			EGL_NONE
		};

		eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
		eglInitialize(eglDisplay, NULL, NULL);
		eglBindAPI(EGL_OPENGL_ES_API);

		EGLint numConfigs;
		eglChooseConfig(eglDisplay, attribs, &eglConfig, 1, &numConfigs);

		EGLint contextAttribs[ 3 ] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
		eglContext = eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, contextAttribs);

		RWindow& window = Window();
		eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig, &window, NULL);
		
		eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);
		
		setlocale(LC_ALL, "");	
		setlocale(LC_CTYPE, "C");
		setlocale(LC_COLLATE, "C");
		setlocale(LC_NUMERIC, "C");
		
		args.exitAtFrame = -1;
#ifdef ENABLE_VM_TRACING
		args.traceBytecodeAfterFrame = 0;
#endif
		args.speedMultiplier = 1.0;
		args.fastForwardSpeed = 0.0;
		args.osType = OS_WINDOWS;
		args.profilerFramesBetween = 0;
		args.loadType = DATAWINLOADTYPE_LOAD_PER_CHUNK;
		args.lazyRooms = true;
		args.lazyTextures = true;
		args.lazyAudio = true;
		args.renderer = MODERN_GL;
		args.dataWinPath = "E:/butterscotch/data.win";
		args.saveFolder = "E:/butterscotch/";
		
		args.debug = true;

		RestartTimerL(10000);
	}

	void HandleWsEventL(const TWsEvent &aEvent, CCoeControl *aDestination) {
		if (!foreground || iAppUi->IsDisplayingDialog()) return;
		switch (aEvent.Type()) {
		case EEventKeyDown:
		case EEventKeyUp: {
			//  TODO
			break;
		}
		default:
			break;
		}
	}
	
	void HandleResourceChange(TInt aType) {
		switch (aType) {
		case KEikDynamicLayoutVariantSwitch:
			SetExtentToWholeScreen();
			break;
		}
	}
};

static ButterscotchContainer* container;

class ButterscotchAppUi : public CAknAppUi {
	ButterscotchContainer* iContainer;
public:
	void ConstructL() {
		BaseConstructL(CAknAppUi::EAknEnableSkin);
		iContainer = new (ELeave) ButterscotchContainer;
		iContainer->SetMopParent(this);
		iContainer->ConstructL(ClientRect(), this);
		container = iContainer;
		AddToStackL(iContainer);
	}
	void HandleForegroundEventL(TBool aForeground) {
		foreground = aForeground;
	}

	~ButterscotchAppUi() {
		if (iContainer) {
			RemoveFromStack(iContainer);
			delete iContainer;
		}
	}
	void HandleCommandL(TInt aCommand) {
		if (aCommand == EAknSoftkeyBack || aCommand == EEikCmdExit) {
			shouldExit = true;
		}
	}
};

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

ButterscotchDocument::ButterscotchDocument(CEikApplication& aApp) : CAknDocument(aApp) {}

ButterscotchDocument::~ButterscotchDocument() {}

void ButterscotchDocument::ConstructL() {}

ButterscotchDocument* ButterscotchDocument::NewL(CEikApplication& aApp) {
	ButterscotchDocument* self = new (ELeave) ButterscotchDocument(aApp);
	CleanupStack::PushL(self);
	self->ConstructL();
	CleanupStack::Pop();
	return self;
}

CEikAppUi* ButterscotchDocument::CreateAppUiL() {
	return new (ELeave) ButterscotchAppUi;
}

class ButterscotchApp: public CAknApplication {
private:
	CApaDocument* CreateDocumentL();
	TUid AppDllUid() const;
};

LOCAL_C CApaApplication* NewApplication();

GLDEF_C TInt E32Main();

TUid ButterscotchApp::AppDllUid() const {
	return TUid::Uid(0xEC93FBDE);
}

CApaDocument* ButterscotchApp::CreateDocumentL() {
	return ButterscotchDocument::NewL(*this);
}

CApaApplication* NewApplication() {
	return new ButterscotchApp;
}

TInt E32Main() {
	User::SetFloatingPointMode(EFpModeRunFast);
	return EikStart::RunApplication(NewApplication);
}



extern "C" bool platformInit(int32_t reqW, int32_t reqH, const char *title, bool headless) {
	return true;
}

extern "C" void platformExit(void) {
//	quit = true;
}

static bool platformGetWindowFocus(void) {
    return foreground;
}

extern "C" void platformInitFunctions(Runner *runner) {
    g_runner = runner;
    runner->setCursor = NULL;
    runner->windowHasFocus = platformGetWindowFocus;
    runner->currentCursor = GML_CR_DEFAULT;
}

extern "C" bool platformGetWindowSize(int32_t* outW, int32_t* outH) {
    if (!outW || !outH || !container) return false;
    TSize size = container->Size();
    *outW = size.iWidth;
    *outH = size.iHeight;
    return true;
}

extern "C" bool platformGetScaledWindowSize(int32_t* outW, int32_t* outH) {
    return platformGetWindowSize(outW, outH);
}

extern "C" void platformSetWindowSize(int32_t width, int32_t height) {
}

extern "C" void platformSetWindowTitle(const char* title) {
}

extern "C" void platformGetMousePos(double *xPos, double *yPos) {
    if (!xPos || !yPos) return;
    *xPos = 0.0;
    *yPos = 0.0;
}

extern "C" void platformSwapBuffers(void) {
	eglSwapBuffers(eglDisplay, eglSurface);
}

extern "C" void *platformGetProcAddress(const char *name) {
	return NULL;
}

extern "C" bool platformHandleEvents(void) {
    return shouldExit;
}

extern "C" void platformSleepUntil(uint64_t time) {
    int64_t remaining = (int64_t)time - (int64_t)nowNanos();
    if (remaining > 2000000) {
        remaining -= 1000000;
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = remaining;
        nanosleep(&ts, NULL);
    }
    while (nowNanos() < time) {
        YIELD();
    }
}
