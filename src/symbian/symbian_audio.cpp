// miniaudio custom driver implementation for symbian

#include <e32base.h>
#include <MdaAudioOutputStream.h>
#include <mda/common/audio.h>
#include <remconcoreapitargetobserver.h>
#include <remconinterfaceselector.h>
#include <remconcoreapitarget.h>
#include <string.h>
#include "miniaudio.h"

extern "C" {
#include "log.h"
}

#define PERIOD_SIZE 2048
#define BUFFER_SIZE (PERIOD_SIZE * 2 * sizeof(TInt16))

#define MAX_VOLUME 100
#define DEFAULT_VOLUME 50
#define VOLUME_STEP 10

class CAudioStream;

static ma_context context;
static CAudioStream* stream;

static int volume = DEFAULT_VOLUME;
static bool volumeChanged;

static void InitVolumeKeysListenerL();

class CAudioStream: public CBase, public MMdaAudioOutputStreamCallback
{
public:
	static CAudioStream* NewL(ma_device* aDevice);
	virtual ~CAudioStream();
	virtual void MaoscOpenComplete(TInt aError);
	virtual void MaoscBufferCopied(TInt aError, const TDesC8& aBuffer);
	virtual void MaoscPlayComplete(TInt aError);
	void Open();
	void Stop();
	void Start();
	void Request();

protected:
	CAudioStream(ma_device* aDevice);
	void ConstructL();

private:
	CMdaAudioOutputStream* iOutputStream;
	TMdaAudioDataSettings iAudioSettings;
	TPtrC8 iPtr;
	ma_device* iDevice;
	TBool iOpen;
	TBool iStarted;
	TUint8 iBuffers[2][BUFFER_SIZE];
	bool iCurrentBuffer;
};

CAudioStream::CAudioStream(ma_device* aDevice) :
	iOutputStream(NULL), iDevice(aDevice), iOpen(EFalse), iStarted(EFalse)
{
}

CAudioStream* CAudioStream::NewL(ma_device* aDevice) {
	CAudioStream* self = new (ELeave) CAudioStream(aDevice);
	CleanupStack::PushL(self);
	self->ConstructL();
	CleanupStack::Pop(self);
	return self;
}

void CAudioStream::ConstructL() {
	iOutputStream = CMdaAudioOutputStream::NewL(*this);
	iCurrentBuffer = 0;
}

CAudioStream::~CAudioStream() {
	if (iOpen) {
		Stop();
	}
	User::After(100000);
	delete iOutputStream;
}

void CAudioStream::Open() {
	iAudioSettings.iCaps = TMdaAudioDataSettings::ERealTime | TMdaAudioDataSettings::ESampleRateFixed;
	iOutputStream->Open(&iAudioSettings);
}

void CAudioStream::Stop() {
	iStarted = EFalse;
	if (iOpen) {
		iOutputStream->Stop();
		iOpen = EFalse;
	}
}

void CAudioStream::Start() {
	iStarted = ETrue;
	if (iOpen) {
		Request();
	} else {
		Open();
	}
}

void CAudioStream::MaoscOpenComplete(TInt aError) {
	if (aError != KErrNone) {
		logError("Audio: Failed to open audio stream: %d", aError);
		return;
	}
	
	iOpen = ETrue;
	iOutputStream->SetPriority(EMdaPriorityNormal, EMdaPriorityPreferenceTime);
	TRAPD(err, iOutputStream->SetAudioPropertiesL(TMdaAudioDataSettings::ESampleRate44100Hz, TMdaAudioDataSettings::EChannelsStereo));
	if (err != KErrNone) {
		logError("Audio: Failed to set audio properties: %d", err);
		return;
	}
	
	iOutputStream->SetVolume((volume * iOutputStream->MaxVolume()) / MAX_VOLUME);
	if (iStarted) {
		Request();
	}
}

void CAudioStream::MaoscBufferCopied(TInt aError, const TDesC8& aBuffer) {
	if (aError == KErrNone && iStarted) {
		Request();
	} else {
		iOutputStream->Stop();
	}
}

void CAudioStream::MaoscPlayComplete(TInt aError) {
	if (iStarted && aError == KErrNone) {
		Request();
	}
}

void CAudioStream::Request() {
	if (!iOpen || !iStarted)
		return;

	TUint8* buffer = iBuffers[iCurrentBuffer ? 1 : 0];
	if (ma_device_handle_backend_data_callback(iDevice, (ma_uint8*)buffer, NULL, PERIOD_SIZE) != MA_SUCCESS)
		return;
	
	if (volumeChanged) {
		volumeChanged = false;
		iOutputStream->SetVolume((volume * iOutputStream->MaxVolume()) / 100);
	}

	iPtr.Set(buffer, BUFFER_SIZE);
	TRAP_IGNORE(iOutputStream->WriteL(iPtr));
	
	iCurrentBuffer = !iCurrentBuffer;
}

static ma_result onDeviceInit(ma_device* pDevice, const ma_device_config* pConfig, ma_device_descriptor* pDescriptorPlayback, ma_device_descriptor* pDescriptorCapture) {
	if (pDevice->type != ma_device_type_playback) {
		return MA_DEVICE_TYPE_NOT_SUPPORTED;
	}

	pDescriptorPlayback->format = ma_format_s16;
	pDescriptorPlayback->channels = 2;
	pDescriptorPlayback->sampleRate = 44100;
	pDescriptorPlayback->periodSizeInFrames = PERIOD_SIZE;
	
	TRAPD(err, stream = CAudioStream::NewL(pDevice));
	if (err != KErrNone) {
		return MA_FAILED_TO_INIT_BACKEND;
	}
	
	TRAP_IGNORE(InitVolumeKeysListenerL());

	return MA_SUCCESS;
}

static ma_result onDeviceUninit(ma_device* pDevice) {
	if (stream) {
		delete stream;
		stream = NULL;
	}
	return MA_SUCCESS;
}

static ma_result onDeviceStart(ma_device* pDevice) {
	if (stream) {
		stream->Start();
	}
	return MA_SUCCESS;
}

static ma_result onDeviceStop(ma_device* pDevice) {
	if (stream) {
		stream->Stop();
	}
	return MA_SUCCESS;
}

static ma_result onContextEnumerateDevices(ma_context* pContext, ma_enum_devices_callback_proc onDevice, void* pUserData) {
	ma_device_info info;
	memset(&info, 0, sizeof(info));
	strcpy(info.name, "Symbian");
	info.isDefault = MA_TRUE;
	onDevice(pContext, ma_device_type_playback, &info, pUserData);
	return MA_SUCCESS;
}

static ma_result onContextUninit(ma_context* pContext) {
	return MA_SUCCESS;
}

ma_result onContextInit(ma_context* pContext, const ma_context_config* pConfig, ma_backend_callbacks* pCallbacks) {
	pCallbacks->onContextUninit = onContextUninit;
	pCallbacks->onContextEnumerateDevices = onContextEnumerateDevices;
	pCallbacks->onDeviceInit = onDeviceInit;
	pCallbacks->onDeviceUninit = onDeviceUninit;
	pCallbacks->onDeviceStart = onDeviceStart;
	pCallbacks->onDeviceStop = onDeviceStop;

	return MA_SUCCESS;
}

extern "C" ma_context* maSymbianAudioInit(void) {
	ma_backend backends[] = { ma_backend_custom };
	ma_context_config contextConfig = ma_context_config_init();
	contextConfig.custom.onContextInit = onContextInit;
	
	ma_result res = ma_context_init(backends, sizeof(backends) / sizeof(backends[0]), &contextConfig, &context);
	if (res != MA_SUCCESS) {
		return NULL;
	}

	return &context;
}

extern "C" void maSymbianAudioDestroy(void) {
	ma_context_uninit(&context);
}

// volume keys listener

class CRemConObserver : public CBase, public MRemConCoreApiTargetObserver
{
public:
	static CRemConObserver* NewL() {
		CRemConObserver* self = new (ELeave) CRemConObserver();
		CleanupStack::PushL(self);
		self->ConstructL();
		CleanupStack::Pop(self);
		return self;
	}
	
	~CRemConObserver() {
//		if (iRemConTarget) {
//			delete iRemConTarget;
//		}
		if (iRemConSelector) {
			delete iRemConSelector;
		}
	}

private:
	CRemConObserver() {}
	
	void ConstructL() {
		iRemConSelector = CRemConInterfaceSelector::NewL();
		iRemConTarget = CRemConCoreApiTarget::NewL(*iRemConSelector, *this);
		iRemConSelector->OpenTargetL();
	}

	void MrccatoCommand(TRemConCoreApiOperationId aOperationId, TRemConCoreApiButtonAction aButtonAct) {
		switch (aOperationId) {
		case ERemConCoreApiVolumeUp: {
			volume += VOLUME_STEP;
			if (volume > MAX_VOLUME) volume = MAX_VOLUME;

			volumeChanged = true;
			break;
		}
		case ERemConCoreApiVolumeDown: {
			volume -= VOLUME_STEP;
			if (volume < 0) volume = 0;
			
			volumeChanged = true;
			break;
		}
		default:
			break;
		}
	}

private:
	CRemConInterfaceSelector* iRemConSelector;
	CRemConCoreApiTarget* iRemConTarget;
};

static CRemConObserver* observer = NULL;

void InitVolumeKeysListenerL() {
	if (!observer) observer = CRemConObserver::NewL();
}
