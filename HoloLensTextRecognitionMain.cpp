#include "pch.h"
#include "HoloLensTextRecognitionMain.h"
#include "TextRecognitionHelper.h"
#include "Common\DirectXHelper.h"
#include "opencv2/text.hpp"
#include "opencv2/core/utility.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/features2d.hpp"

#include <iostream>

#include <windows.graphics.directx.direct3d11.interop.h>
#include <Collection.h>

#include "Audio/OmnidirectionalSound.h"
#include "Content/ShaderStructures.h"


using namespace HoloLensTextRecognition;

using namespace concurrency;
using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Graphics::Holographic;
using namespace Windows::Media::SpeechRecognition;
using namespace Windows::Perception::Spatial;
using namespace Windows::UI::Input::Spatial;
using namespace std::placeholders;
using namespace std;
using namespace cv;
using namespace cv::text;


//Beginning of OpenCV declarations
//ERStat extraction is done in parallel for different channels


//OCR recognition is done in parallel for different detections
template <class T>
class Parallel_OCR : public cv::ParallelLoopBody
{
private:
	vector<Mat> &detections;
	vector<string> &outputs;
	vector< vector<cv::Rect> > &boxes;
	vector< vector<string> > &words;
	vector< vector<float> > &confidences;
	vector< Ptr<T> > &ocrs;

public:
	Parallel_OCR(vector<Mat> &_detections, vector<string> &_outputs, vector< vector<cv::Rect> > &_boxes,
		vector< vector<string> > &_words, vector< vector<float> > &_confidences,
		vector< Ptr<T> > &_ocrs)
		: detections(_detections), outputs(_outputs), boxes(_boxes), words(_words),
		confidences(_confidences), ocrs(_ocrs)
	{}

	virtual void operator()(const cv::Range &r) const
	{
		for (int c = r.start; c < r.end; c++)
		{
			ocrs[c%ocrs.size()]->run(detections[c], outputs[c], &boxes[c], &words[c], &confidences[c], OCR_LEVEL_WORD);
		}
	}
	Parallel_OCR & operator=(const Parallel_OCR &a);
};

class Parallel_extractCSER : public cv::ParallelLoopBody
{
private:
	vector<Mat> &channels;
	vector< vector<ERStat> > &regions;
	vector< Ptr<ERFilter> > er_filter1;
	vector< Ptr<ERFilter> > er_filter2;

public:
	Parallel_extractCSER(vector<Mat> &_channels, vector< vector<ERStat> > &_regions,
		vector<Ptr<ERFilter> >_er_filter1, vector<Ptr<ERFilter> >_er_filter2)
		: channels(_channels), regions(_regions), er_filter1(_er_filter1), er_filter2(_er_filter2) {}

	virtual void operator()(const cv::Range &r) const
	{
		for (int c = r.start; c < r.end; c++)
		{
			er_filter1[c]->run(channels[c], regions[c]);
			er_filter2[c]->run(channels[c], regions[c]);
		}
	}
	Parallel_extractCSER & operator=(const Parallel_extractCSER &a);
};

/*Parallel_extractCSER & Parallel_extractCSER::operator=(const Parallel_extractCSER & a)
{
// TODO: insert return statement here

}*/


//Discard wrongly recognised strings
bool   isRepetitive(const string& s);
//Draw ER's in an image via floodFill
void   er_draw(vector<Mat> &channels, vector<vector<ERStat> > &regions, vector<Vec2i> group, Mat& segmentation);



//Beginning of HoloLens Dependent Declarations

// Loads and initializes application assets when the application is loaded.
HoloLensTextRecognitionMain::HoloLensTextRecognitionMain(const std::shared_ptr<DX::DeviceResources>& deviceResources) :
    m_deviceResources(deviceResources), mTextPosition(0.0f, 340.0f), m_fontLoaded(false)
{
    // Register to be notified if the device is lost or recreated.
    m_deviceResources->RegisterDeviceNotify(this);

	InitializeSpeechCommandList();
}

void HoloLensTextRecognitionMain::InitializeSpeechCommandList()
{
	m_lastCommand = nullptr;
	m_listening = false;
	m_speechCommandData = ref new Platform::Collections::Map<Platform::String^, float4>();

	m_speechCommandData->Insert(L"increase", float4(1.0f, 1.0f, 1.0f, 1.f));
	m_speechCommandData->Insert(L"decrease", float4(0.5f, 0.5f, 0.5f, 1.f));
	m_speechCommandData->Insert(L"left", float4(0.0f, 1.0f, 0.0f, 1.f));
	m_speechCommandData->Insert(L"right", float4(0.1f, 0.1f, 0.1f, 1.f));
	m_speechCommandData->Insert(L"stop", float4(1.0f, 0.0f, 0.0f, 1.f));
	m_speechCommandData->Insert(L"start", float4(1.0f, 1.0f, 0.0f, 1.f));

	// You can use non-dictionary words as speech commands.
	m_speechCommandData->Insert(L"SpeechRecognizer", float4(0.5f, 0.1f, 1.f, 1.f));
}

void HoloLensTextRecognitionMain::BeginVoiceUIPrompt()
{
	// RecognizeWithUIAsync provides speech recognition begin and end prompts, but it does not provide
	// synthesized speech prompts. Instead, you should provide your own speech prompts when requesting
	// phrase input.
	// Here is an example of how to do that with a speech synthesizer. You could also use a pre-recorded 
	// voice clip, a visual UI, or other indicator of what to say.
	auto speechSynthesizer = ref new Windows::Media::SpeechSynthesis::SpeechSynthesizer();

	StringReference voicePrompt;

	// A command list is used to continuously look for one-word commands.
	// You need some way for the user to know what commands they can say. In this example, we provide
	// verbal instructions; you could also use graphical UI, etc.
	voicePrompt = L"Say right, left, increased or decrease at any time to alter the cube.";

	// Kick off speech synthesis.
	create_task(speechSynthesizer->SynthesizeTextToStreamAsync(voicePrompt), task_continuation_context::use_current())
		.then([this, speechSynthesizer](task<Windows::Media::SpeechSynthesis::SpeechSynthesisStream^> synthesisStreamTask)
	{
		try
		{
			// The speech synthesis is sent as a byte stream.
			Windows::Media::SpeechSynthesis::SpeechSynthesisStream^ stream = synthesisStreamTask.get();

			// We can initialize an XAudio2 voice using that byte stream.
			// Here, we use it to play an HRTF audio effect.
			auto hr = m_speechSynthesisSound.Initialize(stream, 0);
			if (SUCCEEDED(hr))
			{
				m_speechSynthesisSound.SetEnvironment(HrtfEnvironment::Small);
				m_speechSynthesisSound.Start();

				// Amount of time to pause after the audio prompt is complete, before listening 
				// for speech input.
				static const float bufferTime = 0.15f;

				// Wait until the prompt is done before listening.
				m_secondsUntilSoundIsComplete = m_speechSynthesisSound.GetDuration() + bufferTime;
				m_waitingForSpeechPrompt = true;
			}
		}
		catch (Platform::Exception^ exception)
		{
			PrintWstringToDebugConsole(
				std::wstring(L"Exception while trying to synthesize speech: ") +
				exception->Message->Data() +
				L"\n"
			);

			// Handle exceptions here.
		}
	});
}

void HoloLensTextRecognitionMain::PlayRecognitionBeginSound()
{
	// The user needs a cue to begin speaking. We will play this sound effect just before starting 
	// the recognizer.
	auto hr = m_startRecognitionSound.GetInitializationStatus();
	if (SUCCEEDED(hr))
	{
		m_startRecognitionSound.SetEnvironment(HrtfEnvironment::Small);
		m_startRecognitionSound.Start();

		// Wait until the audible cue is done before starting to listen.
		m_secondsUntilSoundIsComplete = m_startRecognitionSound.GetDuration();
		m_waitingForSpeechCue = true;
	}
}

void HoloLensTextRecognitionMain::PlayRecognitionSound()
{
	// The user should be given a cue when recognition is complete. 
	auto hr = m_recognitionSound.GetInitializationStatus();
	if (SUCCEEDED(hr))
	{
		// re-initialize the sound so it can be replayed.
		m_recognitionSound.Initialize(L"Audio//BasicResultsEarcon.wav", 0);
		m_recognitionSound.SetEnvironment(HrtfEnvironment::Small);

		m_recognitionSound.Start();
	}
}

Concurrency::task<void> HoloLensTextRecognitionMain::StopCurrentRecognizerIfExists()
{
	if (m_speechRecognizer != nullptr)
	{
		return create_task(m_speechRecognizer->StopRecognitionAsync()).then([this]()
		{
			m_speechRecognizer->RecognitionQualityDegrading -= m_speechRecognitionQualityDegradedToken;

			if (m_speechRecognizer->ContinuousRecognitionSession != nullptr)
			{
				m_speechRecognizer->ContinuousRecognitionSession->ResultGenerated -= m_speechRecognizerResultEventToken;
			}
		});
	}
	else
	{
		return create_task([this]() {});
	}
}

bool HoloLensTextRecognitionMain::InitializeSpeechRecognizer()
{
	m_speechRecognizer = ref new SpeechRecognizer();

	if (!m_speechRecognizer)
	{
		return false;
	}

	m_speechRecognitionQualityDegradedToken = m_speechRecognizer->RecognitionQualityDegrading +=
		ref new TypedEventHandler<SpeechRecognizer^, SpeechRecognitionQualityDegradingEventArgs^>(
			std::bind(&HoloLensTextRecognitionMain::OnSpeechQualityDegraded, this, _1, _2)
			);

	m_speechRecognizerResultEventToken = m_speechRecognizer->ContinuousRecognitionSession->ResultGenerated +=
		ref new TypedEventHandler<SpeechContinuousRecognitionSession^, SpeechContinuousRecognitionResultGeneratedEventArgs^>(
			std::bind(&HoloLensTextRecognitionMain::OnResultGenerated, this, _1, _2)
			);

	return true;
}

task<bool> HoloLensTextRecognitionMain::StartRecognizeSpeechCommands()
{
	return StopCurrentRecognizerIfExists().then([this]()
	{
		if (!InitializeSpeechRecognizer())
		{
			return task_from_result<bool>(false);
		}

		// Here, we compile the list of voice commands by reading them from the map.
		Platform::Collections::Vector<Platform::String^>^ speechCommandList = ref new Platform::Collections::Vector<Platform::String^>();
		for each (auto pair in m_speechCommandData)
		{
			// The speech command string is what we are looking for here. Later, we can use the
			// recognition result for this string to look up a color value.
			auto command = pair->Key;

			// Add it to the list.
			speechCommandList->Append(command);
		}

		SpeechRecognitionListConstraint^ spConstraint = ref new SpeechRecognitionListConstraint(speechCommandList);
		m_speechRecognizer->Constraints->Clear();
		m_speechRecognizer->Constraints->Append(spConstraint);
		return create_task(m_speechRecognizer->CompileConstraintsAsync()).then([this](task<SpeechRecognitionCompilationResult^> previousTask)
		{
			try
			{
				SpeechRecognitionCompilationResult^ compilationResult = previousTask.get();

				if (compilationResult->Status == SpeechRecognitionResultStatus::Success)
				{
					// If compilation succeeds, we can start listening for results.
					return create_task(m_speechRecognizer->ContinuousRecognitionSession->StartAsync()).then([this](task<void> startAsyncTask) {

						try
						{
							// StartAsync may throw an exception if your app doesn't have Microphone permissions. 
							// Make sure they're caught and handled appropriately (otherwise the app may silently not work as expected)
							startAsyncTask.get();
							return true;
						}
						catch (Platform::Exception^ exception)
						{
							PrintWstringToDebugConsole(
								std::wstring(L"Exception while trying to start speech Recognition: ") +
								exception->Message->Data() +
								L"\n"
							);

							return false;
						}
					});
				}
				else
				{
					OutputDebugStringW(L"Could not initialize constraint-based speech engine!\n");

					// Handle errors here.
					return create_task([this] {return false; });
				}
			}
			catch (Platform::Exception^ exception)
			{
				// Note that if you get an "Access is denied" exception, you might need to enable the microphone 
				// privacy setting on the device and/or add the microphone capability to your app manifest.

				PrintWstringToDebugConsole(
					std::wstring(L"Exception while trying to initialize speech command list:") +
					exception->Message->Data() +
					L"\n"
				);

				// Handle exceptions here.
				return create_task([this] {return false; });
			}
		});
	});
}

bool isRepetitive(const string& s)
{
	int count = 0;
	int count2 = 0;
	int count3 = 0;
	int first = (int)s[0];
	int last = (int)s[(int)s.size() - 1];
	for (int i = 0; i<(int)s.size(); i++)
	{
		if ((s[i] == 'i') ||
			(s[i] == 'l') ||
			(s[i] == 'I'))
			count++;
		if ((int)s[i] == first)
			count2++;
		if ((int)s[i] == last)
			count3++;
	}
	if ((count > ((int)s.size() + 1) / 2) || (count2 == (int)s.size()) || (count3 > ((int)s.size() * 2) / 3))
	{
		return true;
	}


	return false;
}

void er_draw(vector<Mat> &channels, vector<vector<ERStat> > &regions, vector<Vec2i> group, Mat& segmentation)
{
	for (int r = 0; r<(int)group.size(); r++)
	{
		ERStat er = regions[group[r][0]][group[r][1]];
		if (er.parent != NULL) // deprecate the root region
		{
			int newMaskVal = 255;
			int flags = 4 + (newMaskVal << 8) + FLOODFILL_FIXED_RANGE + FLOODFILL_MASK_ONLY;
			floodFill(channels[group[r][0]], segmentation, cv::Point(er.pixel%channels[group[r][0]].cols, er.pixel / channels[group[r][0]].cols),
				Scalar(255), 0, Scalar(er.level), Scalar(0), flags);
		}
	}
}

void HoloLensTextRecognitionMain::SetHolographicSpace(HolographicSpace^ holographicSpace)
{
    UnregisterHolographicEventHandlers();

    m_holographicSpace = holographicSpace;

    //
    // TODO: Add code here to initialize your holographic content.
    //
	mRenderState = std::make_shared<RenderStateHelper>(m_deviceResources->GetD3DDeviceContext());

	mSpriteBatch = std::make_unique<DirectX::SpriteBatch>(m_deviceResources->GetD3DDeviceContext());
	std::wstring spriteFontFileName = L"ms-appx:///Content\\Arial_28_Regular.spritefont";
	std::wstring spriteFontFileName2 = L"ms-appx:///Content\\Verdana_30_Regular.spritefont";
	std::wstring spriteFontFileName3 = L"ms-appx:///Content\\Nuwaupic_Line_Font_30_Regular.spritefont";
	
	//Load font file asynchronously
	task<std::vector<byte>> loadVSTask = DX::ReadDataAsync(spriteFontFileName);
	task<void> createVSTask = loadVSTask.then([this](const std::vector<byte>& fileData) {
		size_t fileSize = fileData.size();
		uint8_t const* blob = fileData.data();

		mSpriteFont = std::make_unique<DirectX::SpriteFont>((ID3D11Device*)m_deviceResources->GetD3DDevice(), blob, fileSize);
		m_fontLoaded = true;
	});

	createVSTask.then([](task<void> t) {
		try {
			t.get();
			OutputDebugString(L"Retrieved Font File");
		}
		catch (Platform::COMException ^ e)
		{
			OutputDebugString(e->Message->Data());

		}
	});

#ifdef DRAW_SAMPLE_CONTENT
    // Initialize the sample hologram.
    m_spinningCubeRenderer = std::make_unique<SpinningCubeRenderer>(m_deviceResources);

    m_spatialInputHandler = std::make_unique<SpatialInputHandler>();
#endif

    // Use the default SpatialLocator to track the motion of the device.
    m_locator = SpatialLocator::GetDefault();

    // Be able to respond to changes in the positional tracking state.
    m_locatabilityChangedToken =
        m_locator->LocatabilityChanged +=
            ref new Windows::Foundation::TypedEventHandler<SpatialLocator^, Object^>(
                std::bind(&HoloLensTextRecognitionMain::OnLocatabilityChanged, this, _1, _2)
                );

    // Respond to camera added events by creating any resources that are specific
    // to that camera, such as the back buffer render target view.
    // When we add an event handler for CameraAdded, the API layer will avoid putting
    // the new camera in new HolographicFrames until we complete the deferral we created
    // for that handler, or return from the handler without creating a deferral. This
    // allows the app to take more than one frame to finish creating resources and
    // loading assets for the new holographic camera.
    // This function should be registered before the app creates any HolographicFrames.
    m_cameraAddedToken =
        m_holographicSpace->CameraAdded +=
            ref new Windows::Foundation::TypedEventHandler<HolographicSpace^, HolographicSpaceCameraAddedEventArgs^>(
                std::bind(&HoloLensTextRecognitionMain::OnCameraAdded, this, _1, _2)
                );

    // Respond to camera removed events by releasing resources that were created for that
    // camera.
    // When the app receives a CameraRemoved event, it releases all references to the back
    // buffer right away. This includes render target views, Direct2D target bitmaps, and so on.
    // The app must also ensure that the back buffer is not attached as a render target, as
    // shown in DeviceResources::ReleaseResourcesForBackBuffer.
    m_cameraRemovedToken =
        m_holographicSpace->CameraRemoved +=
            ref new Windows::Foundation::TypedEventHandler<HolographicSpace^, HolographicSpaceCameraRemovedEventArgs^>(
                std::bind(&HoloLensTextRecognitionMain::OnCameraRemoved, this, _1, _2)
                );

    // The simplest way to render world-locked holograms is to create a stationary reference frame
    // when the app is launched. This is roughly analogous to creating a "world" coordinate system
    // with the origin placed at the device's position as the app is launched.
    m_referenceFrame = m_locator->CreateStationaryFrameOfReferenceAtCurrentLocation();

    // Notes on spatial tracking APIs:
    // * Stationary reference frames are designed to provide a best-fit position relative to the
    //   overall space. Individual positions within that reference frame are allowed to drift slightly
    //   as the device learns more about the environment.
    // * When precise placement of individual holograms is required, a SpatialAnchor should be used to
    //   anchor the individual hologram to a position in the real world - for example, a point the user
    //   indicates to be of special interest. Anchor positions do not drift, but can be corrected; the
    //   anchor will use the corrected position starting in the next frame after the correction has
    //   occurred.

	// Preload audio assets for audio cues.
	m_startRecognitionSound.Initialize(L"Audio//BasicListeningEarcon.wav", 0);
	m_recognitionSound.Initialize(L"Audio//BasicResultsEarcon.wav", 0);

	// Begin the code sample scenario.
	BeginVoiceUIPrompt();
}

void HoloLensTextRecognitionMain::UnregisterHolographicEventHandlers()
{
    if (m_holographicSpace != nullptr)
    {
        // Clear previous event registrations.

        if (m_cameraAddedToken.Value != 0)
        {
            m_holographicSpace->CameraAdded -= m_cameraAddedToken;
            m_cameraAddedToken.Value = 0;
        }

        if (m_cameraRemovedToken.Value != 0)
        {
            m_holographicSpace->CameraRemoved -= m_cameraRemovedToken;
            m_cameraRemovedToken.Value = 0;
        }
    }

    if (m_locator != nullptr)
    {
        m_locator->LocatabilityChanged -= m_locatabilityChangedToken;
    }
}

HoloLensTextRecognitionMain::~HoloLensTextRecognitionMain()
{
    // Deregister device notification.
    m_deviceResources->RegisterDeviceNotify(nullptr);

    UnregisterHolographicEventHandlers();
}

// Updates the application state once per frame.
HolographicFrame^ HoloLensTextRecognitionMain::Update()
{
    // Before doing the timer update, there is some work to do per-frame
    // to maintain holographic rendering. First, we will get information
    // about the current frame.

    // The HolographicFrame has information that the app needs in order
    // to update and render the current frame. The app begins each new
    // frame by calling CreateNextFrame.
    HolographicFrame^ holographicFrame = m_holographicSpace->CreateNextFrame();

    // Get a prediction of where holographic cameras will be when this frame
    // is presented.
    HolographicFramePrediction^ prediction = holographicFrame->CurrentPrediction;

    // Back buffers can change from frame to frame. Validate each buffer, and recreate
    // resource views and depth buffers as needed.
    m_deviceResources->EnsureCameraResources(holographicFrame, prediction);

    // Next, we get a coordinate system from the attached frame of reference that is
    // associated with the current frame. Later, this coordinate system is used for
    // for creating the stereo view matrices when rendering the sample content.
    SpatialCoordinateSystem^ currentCoordinateSystem = m_referenceFrame->CoordinateSystem;

#ifdef DRAW_SAMPLE_CONTENT
    // Check for new input state since the last frame.
    SpatialInteractionSourceState^ pointerState = m_spatialInputHandler->CheckForInput();
    if (pointerState != nullptr)
    {
        // When a Pressed gesture is detected, the sample hologram will be repositioned
        // two meters in front of the user.
        m_spinningCubeRenderer->PositionHologram(
            pointerState->TryGetPointerPose(currentCoordinateSystem)
            );
    }
#endif
	// Check for new speech input since the last frame.
	if (m_lastCommand != nullptr)
	{
		auto command = m_lastCommand;
		m_lastCommand = nullptr;

		// Check to see if the spoken word or phrase, matches up with any of the speech
		// commands in our speech command map.
		for each (auto& iter in m_speechCommandData)
		{
			std::wstring lastCommandString = command->Data();
			std::wstring listCommandString = iter->Key->Data();

			if (lastCommandString.find(listCommandString) != std::wstring::npos)
			{
				if (lastCommandString._Equal(L"right"))
				{
					m_spinningCubeRenderer->RotateRight(degrees);
				}
				if (lastCommandString._Equal(L"left"))
				{
					m_spinningCubeRenderer->RotateLeft(degrees);
				}
				if (lastCommandString._Equal(L"increase"))
				{
					m_spinningCubeRenderer->ZoomIn(scale);
				}
				if (lastCommandString._Equal(L"decrease"))
				{
					m_spinningCubeRenderer->ZoomOut(scale);
				}
				if (lastCommandString._Equal(L"stop"))
				{
					//Stop rendering
				}
				if (lastCommandString._Equal(L"start"))
				{
					//Start rendering
				}
			}
		}
	}

    m_timer.Tick([&] ()
    {
        //
        // TODO: Update scene objects.
        //
        // Put time-based updates here. By default this code will run once per frame,
        // but if you change the StepTimer to use a fixed time step this code will
        // run as many times as needed to get to the current step.
        //


#ifdef DRAW_SAMPLE_CONTENT
        m_spinningCubeRenderer->Update(m_timer);
#endif

		// Wait to listen for speech input until the audible UI prompts are complete.
		if ((m_waitingForSpeechPrompt == true) &&
			((m_secondsUntilSoundIsComplete -= static_cast<float>(m_timer.GetElapsedSeconds())) <= 0.f))
		{
			m_waitingForSpeechPrompt = false;
			PlayRecognitionBeginSound();
		}
		else if ((m_waitingForSpeechCue == true) &&
			((m_secondsUntilSoundIsComplete -= static_cast<float>(m_timer.GetElapsedSeconds())) <= 0.f))
		{
			m_waitingForSpeechCue = false;
			m_secondsUntilSoundIsComplete = 0.f;
			StartRecognizeSpeechCommands();
		}
    });

    // We complete the frame update by using information about our content positioning
    // to set the focus point.

    for (auto cameraPose : prediction->CameraPoses)
    {
#ifdef DRAW_SAMPLE_CONTENT
        // The HolographicCameraRenderingParameters class provides access to set
        // the image stabilization parameters.
        HolographicCameraRenderingParameters^ renderingParameters = holographicFrame->GetRenderingParameters(cameraPose);

        // SetFocusPoint informs the system about a specific point in your scene to
        // prioritize for image stabilization. The focus point is set independently
        // for each holographic camera.
        // You should set the focus point near the content that the user is looking at.
        // In this example, we put the focus point at the center of the sample hologram,
        // since that is the only hologram available for the user to focus on.
        // You can also set the relative velocity and facing of that content; the sample
        // hologram is at a fixed point so we only need to indicate its position.
        renderingParameters->SetFocusPoint(
            currentCoordinateSystem,
            m_spinningCubeRenderer->GetPosition()
            );
#endif
    }

    // The holographic frame will be used to get up-to-date view and projection matrices and
    // to present the swap chain.
    return holographicFrame;
}

// Renders the current frame to each holographic camera, according to the
// current application and spatial positioning state. Returns true if the
// frame was rendered to at least one camera.
bool HoloLensTextRecognitionMain::Render(Windows::Graphics::Holographic::HolographicFrame^ holographicFrame, TextRecognitionHelper textHelper)
{
    
	TextRecognitionHelper textRecognitionHelper = textHelper;
	// Don't try to render anything before the first Update.
    if (m_timer.GetFrameCount() == 0)
    {
        return false;
    }

    //
    // TODO: Add code for pre-pass rendering here.
    //
    // Take care of any tasks that are not specific to an individual holographic
    // camera. This includes anything that doesn't need the final view or projection
    // matrix, such as lighting maps.
    //
	

		
	/*Text Recognition (OCR)*/
	bool downsize = false;
	int  REGION_TYPE = 1;
	int  GROUPING_ALGORITHM = 0;
	int  RECOGNITION = 0;
	char *region_types_str[2] = { const_cast<char *>("ERStats"), const_cast<char *>("MSER") };
	char *grouping_algorithms_str[2] = { const_cast<char *>("exhaustive_search"), const_cast<char *>("multioriented") };
	char *recognitions_str[2] = { const_cast<char *>("Tesseract"), const_cast<char *>("NM_chain_features + KNN") };

	Mat frame, grey, orig_grey, out_img;
	vector<Mat> channels;
	vector<vector<ERStat> > regions(2);
	vector< Ptr<ERFilter> > er_filters1 = textRecognitionHelper.getERFilters1();
	vector< Ptr<ERFilter> > er_filters2 = textRecognitionHelper.getERFilters2();
	//Function variables @TODO replace with better solution
	//int num_ocrs = 5;
	//int RECOGNITION = 0;
	//vector<cv::Rect> nm_boxes;
	//vector< Ptr<OCRTesseract> > ocrs;
	//vector< Ptr<OCRHMMDecoder> > decoders;


	int cam_idx = 0;
	/*if (argc > 1)
	cam_idx = atoi(argv[1]);*/

	VideoCapture cap(cam_idx);
	if (!cap.isOpened())
	{
		//std::cout << "ERROR: Cannot open default camera (0)." << endl;
		//return -1;
	}
	//double t_all = (double)getTickCount();

	bool capturedFrame = cap.read(frame);
	if (downsize)
		resize(frame, frame, cv::Size(320, 240));

	/*Text Detection*/
	cv::cvtColor(frame, grey, COLOR_RGB2GRAY);
	grey.copyTo(orig_grey);
	// Extract channels to be processed individually
	channels.clear();
	channels.push_back(grey);
	channels.push_back(255 - grey);


	regions[0].clear();
	regions[1].clear();

	cv::parallel_for_(cv::Range(0, (int)channels.size()), Parallel_extractCSER(channels, regions, er_filters1, er_filters2));

	// Detect character groups
	vector< vector<Vec2i> > nm_region_groups;
	vector<cv::Rect> nm_boxes;
	switch (GROUPING_ALGORITHM)
	{
	case 0:
	{
		erGrouping(frame, channels, regions, nm_region_groups, nm_boxes, ERGROUPING_ORIENTATION_HORIZ);
		break;
	}
	case 1:
	{
		erGrouping(frame, channels, regions, nm_region_groups, nm_boxes, ERGROUPING_ORIENTATION_ANY, "./trained_classifier_erGrouping.xml", 0.5);
		break;
	}
	}

	//fframe.copyTo(out_img);
	int scale = 1;//downsize ? 2 : 1;
	float scale_img = (float)((600.f / 300) / scale);;// (float)((600.f / frame.rows) / scale);
	float scale_font = (float)(2 - scale_img) / 1.4f;
	vector<string> words_detection;
	/*float min_confidence1 = 0.f, min_confidence2 = 0.f;
	if (RECOGNITION == 0)
	{
		min_confidence1 = 51.f; min_confidence2 = 60.f;
	}*/
	float min_confidence1 = 51.f, min_confidence2 = 60.f;
	int num_ocrs = 5;//TextRecognitionHelper.getInstance().getNumOCRs();
	vector< Ptr<OCRTesseract> > ocrs; //= TextRecognitionHelper.getInstance().getOCR();
	//vector<cv::Rect> nm_boxes;

	vector<Mat> detections;


	/*for (int i = 0; i<(int)nm_boxes.size(); i++)
	{
		rectangle(out_img, nm_boxes[i].tl(), nm_boxes[i].br(), Scalar(255, 255, 0), 3);

		Mat group_img = Mat::zeros(frame.rows + 2, frame.cols + 2, CV_8UC1);
		er_draw(channels, regions, nm_region_groups[i], group_img);
		group_img(nm_boxes[i]).copyTo(group_img);
		copyMakeBorder(group_img, group_img, 15, 15, 15, 15, BORDER_CONSTANT, Scalar(0));
		detections.push_back(group_img);
	}*/
	vector<string> outputs((int)detections.size());
	vector< vector<cv::Rect> > boxes((int)detections.size());
	vector< vector<string> > words((int)detections.size());
	vector< vector<float> > confidences((int)detections.size());

	// parallel process detections in batches of ocrs.size() (== num_ocrs)
	for (int i = 0; i<(int)detections.size(); i = i + (int)num_ocrs)
	{
		Range r;
		if (i + (int)num_ocrs <= (int)detections.size())
			r = Range(i, i + (int)num_ocrs);
		else
			r = Range(i, (int)detections.size());
		cv::parallel_for_(r, Parallel_OCR<OCRTesseract>(detections, outputs, boxes, words, confidences, ocrs));
	}


	for (int i = 0; i<(int)detections.size(); i++)
	{

		outputs[i].erase(remove(outputs[i].begin(), outputs[i].end(), '\n'), outputs[i].end());
		//cout << "OCR output = \"" << outputs[i] << "\" lenght = " << outputs[i].size() << endl;
		if (outputs[i].size() < 3)
			continue;

		for (int j = 0; j<(int)boxes[i].size(); j++)
		{
			boxes[i][j].x += nm_boxes[i].x - 15;
			boxes[i][j].y += nm_boxes[i].y - 15;
			cout << "  word = " << words[i][j] << "\t confidence = " << confidences[i][j] << endl;
			if ((words[i][j].size() < 2) || (confidences[i][j] < min_confidence1) ||
				((words[i][j].size() == 2) && (words[i][j][0] == words[i][j][1])) ||
				((words[i][j].size()< 4) && (confidences[i][j] < min_confidence2)) ||
				isRepetitive(words[i][j]))
				continue;
			words_detection.push_back(words[i][j]);
			/*rectangle(out_img, boxes[i][j].tl(), boxes[i][j].br(), Scalar(255, 0, 255), 3);
			cv::Size word_size = getTextSize(words[i][j], FONT_HERSHEY_SIMPLEX, (double)scale_font, (int)(3 * scale_font), NULL);
			rectangle(out_img, boxes[i][j].tl() - cv::Point(3, word_size.height + 3), boxes[i][j].tl() + cv::Point(word_size.width, 0), Scalar(255, 0, 255), -1);
			putText(out_img, words[i][j], boxes[i][j].tl() - cv::Point(1, 1), FONT_HERSHEY_SIMPLEX, scale_font, Scalar(255, 255, 255), (int)(3 * scale_font));*/
		}

	}

	/*t_all = ((double)getTickCount() - t_all) * 1000 / getTickFrequency();
	char buff[100];
	sprintf(buff, "%2.1f Fps. @ %dx%d", (float)(1000 / t_all), out_img.cols, out_img.rows);
	string fps_info = buff;
	rectangle(out_img, cv::Point(out_img.rows - (160 / scale), out_img.rows - (70 / scale)), cv::Point(out_img.cols, out_img.rows), Scalar(255, 255, 255), -1);
	putText(out_img, fps_info, cv::Point(10, out_img.rows - (10 / scale)), FONT_HERSHEY_DUPLEX, scale_font, Scalar(255, 0, 0));
	putText(out_img, region_types_str[REGION_TYPE], cv::Point(out_img.rows - (150 / scale), out_img.rows - (50 / scale)), FONT_HERSHEY_DUPLEX, scale_font, Scalar(255, 0, 0));
	putText(out_img, grouping_algorithms_str[GROUPING_ALGORITHM], cv::Point(out_img.rows - (150 / scale), out_img.rows - (30 / scale)), FONT_HERSHEY_DUPLEX, scale_font, Scalar(255, 0, 0));
	putText(out_img, recognitions_str[RECOGNITION], cv::Point(out_img.rows - (150 / scale), out_img.rows - (10 / scale)), FONT_HERSHEY_DUPLEX, scale_font, Scalar(255, 0, 0));*/

    // Lock the set of holographic camera resources, then draw to each camera
    // in this frame.
    return m_deviceResources->UseHolographicCameraResources<bool>(
        [this, holographicFrame](std::map<UINT32, std::unique_ptr<DX::CameraResources>>& cameraResourceMap)
    {
        // Up-to-date frame predictions enhance the effectiveness of image stablization and
        // allow more accurate positioning of holograms.
        holographicFrame->UpdateCurrentPrediction();
        HolographicFramePrediction^ prediction = holographicFrame->CurrentPrediction;

        bool atLeastOneCameraRendered = false;
        for (auto cameraPose : prediction->CameraPoses)
        {
            // This represents the device-based resources for a HolographicCamera.
            DX::CameraResources* pCameraResources = cameraResourceMap[cameraPose->HolographicCamera->Id].get();

            // Get the device context.
            const auto context = m_deviceResources->GetD3DDeviceContext();
            const auto depthStencilView = pCameraResources->GetDepthStencilView();

            // Set render targets to the current holographic camera.
            ID3D11RenderTargetView *const targets[1] = { pCameraResources->GetBackBufferRenderTargetView() };
            context->OMSetRenderTargets(1, targets, depthStencilView);

            // Clear the back buffer and depth stencil view.
            context->ClearRenderTargetView(targets[0], DirectX::Colors::Transparent);
            context->ClearDepthStencilView(depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

            //
            // TODO: Replace the sample content with your own content.
            //
            // Notes regarding holographic content:
            //    * For drawing, remember that you have the potential to fill twice as many pixels
            //      in a stereoscopic render target as compared to a non-stereoscopic render target
            //      of the same resolution. Avoid unnecessary or repeated writes to the same pixel,
            //      and only draw holograms that the user can see.
            //    * To help occlude hologram geometry, you can create a depth map using geometry
            //      data obtained via the surface mapping APIs. You can use this depth map to avoid
            //      rendering holograms that are intended to be hidden behind tables, walls,
            //      monitors, and so on.
            //    * Black pixels will appear transparent to the user wearing the device, but you
            //      should still use alpha blending to draw semitransparent holograms. You should
            //      also clear the screen to Transparent as shown above.
            //


            // The view and projection matrices for each holographic camera will change
            // every frame. This function refreshes the data in the constant buffer for
            // the holographic camera indicated by cameraPose.
            pCameraResources->UpdateViewProjectionBuffer(m_deviceResources, cameraPose, m_referenceFrame->CoordinateSystem);

            // Attach the view/projection constant buffer for this camera to the graphics pipeline.
            bool cameraActive = pCameraResources->AttachViewProjectionBuffer(m_deviceResources);

#ifdef DRAW_SAMPLE_CONTENT
            // Only render world-locked content when positional tracking is active.
            if (cameraActive)
            {
                // Draw the sample hologram.
                //m_spinningCubeRenderer->Render();

				if (m_fontLoaded)
				{
					const DirectX::XMFLOAT2 & origin{ 0, 0 };
					mTextPosition.x =  cameraPose->Viewport.Width / 2;
					mTextPosition.y =  cameraPose->Viewport.Height / 2;
					mRenderState->SaveAll();

					mSpriteBatch->Begin();

					mSpriteBatch->UpdateViewProjectionBuffer( cameraPose, m_referenceFrame->CoordinateSystem);
					bool spriteCameraActive = mSpriteBatch->AttachViewProjectionBuffer();
					const wchar_t* output = L"Figure 01";

					if (spriteCameraActive)
					{
						
						mSpriteFont->DrawString(mSpriteBatch.get(), output, mTextPosition, Colors::AntiqueWhite, 0.0f, origin, 1.0f, DirectX::SpriteEffects::SpriteEffects_None, -1.0f);
						mSpriteFont->DrawString(mSpriteBatch.get(), output, mTextPosition, Colors::AntiqueWhite, 0.0f, origin, 1.0f, DirectX::SpriteEffects::SpriteEffects_None, -0.999f);
						mSpriteFont->DrawString(mSpriteBatch.get(), output, mTextPosition, Colors::Black, 0.0f, origin, 1.0f, DirectX::SpriteEffects::SpriteEffects_None, -0.998f);
						mSpriteFont->DrawString(mSpriteBatch.get(), output, mTextPosition, Colors::CornflowerBlue, 0.0f, origin, 1.0f, DirectX::SpriteEffects::SpriteEffects_None, -0.997f);
						mSpriteFont->DrawString(mSpriteBatch.get(), output, mTextPosition, Colors::CornflowerBlue, 0.0f, origin, 1.0f, DirectX::SpriteEffects::SpriteEffects_None, -0.996f);
						mSpriteFont->DrawString(mSpriteBatch.get(), output, mTextPosition, Colors::CornflowerBlue, 0.0f, origin, 1.0f, DirectX::SpriteEffects::SpriteEffects_None, -0.995f);
						mSpriteFont->DrawString(mSpriteBatch.get(), output, mTextPosition, Colors::CornflowerBlue, 0.0f, origin, 1.0f, DirectX::SpriteEffects::SpriteEffects_None, -0.994f);
						mSpriteFont->DrawString(mSpriteBatch.get(), output, mTextPosition, Colors::Black, 0.0f, origin, 1.0f, DirectX::SpriteEffects::SpriteEffects_None, -0.993f);
						mSpriteFont->DrawString(mSpriteBatch.get(), output, mTextPosition, Colors::Black, 0.0f, origin, 1.0f, DirectX::SpriteEffects::SpriteEffects_None, -0.992f);
						mSpriteFont->DrawString(mSpriteBatch.get(), output, mTextPosition, Colors::CornflowerBlue, 0.0f, origin, 1.0f, DirectX::SpriteEffects::SpriteEffects_None, -0.991f);
						mSpriteFont->DrawString(mSpriteBatch.get(), output, mTextPosition, Colors::CornflowerBlue, 0.0f, origin, 1.0f, DirectX::SpriteEffects::SpriteEffects_None, -.990f);
						
					}

					mSpriteBatch->End();
					mRenderState->RestoreAll();

				}
            }
#endif
            atLeastOneCameraRendered = true;
        }

        return atLeastOneCameraRendered;
    });
}

void HoloLensTextRecognitionMain::SaveAppState()
{
    //
    // TODO: Insert code here to save your app state.
    //       This method is called when the app is about to suspend.
    //
    //       For example, store information in the SpatialAnchorStore.
    //
}

void HoloLensTextRecognitionMain::LoadAppState()
{
    //
    // TODO: Insert code here to load your app state.
    //       This method is called when the app resumes.
    //
    //       For example, load information from the SpatialAnchorStore.
    //
}

// Notifies classes that use Direct3D device resources that the device resources
// need to be released before this method returns.
void HoloLensTextRecognitionMain::OnDeviceLost()
{
#ifdef DRAW_SAMPLE_CONTENT
    m_spinningCubeRenderer->ReleaseDeviceDependentResources();
#endif
}

// Notifies classes that use Direct3D device resources that the device resources
// may now be recreated.
void HoloLensTextRecognitionMain::OnDeviceRestored()
{
#ifdef DRAW_SAMPLE_CONTENT
    m_spinningCubeRenderer->CreateDeviceDependentResources();
#endif
}

void HoloLensTextRecognitionMain::OnLocatabilityChanged(SpatialLocator^ sender, Object^ args)
{
    switch (sender->Locatability)
    {
    case SpatialLocatability::Unavailable:
        // Holograms cannot be rendered.
        {
            /*String^ message = L"Warning! Positional tracking is " +
                                        sender->Locatability.ToString() + L".\n";
            OutputDebugStringW(message->Data());*/
        }
        break;

    // In the following three cases, it is still possible to place holograms using a
    // SpatialLocatorAttachedFrameOfReference.
    case SpatialLocatability::PositionalTrackingActivating:
        // The system is preparing to use positional tracking.

    case SpatialLocatability::OrientationOnly:
        // Positional tracking has not been activated.

    case SpatialLocatability::PositionalTrackingInhibited:
        // Positional tracking is temporarily inhibited. User action may be required
        // in order to restore positional tracking.
        break;

    case SpatialLocatability::PositionalTrackingActive:
        // Positional tracking is active. World-locked content can be rendered.
        break;
    }
}

void HoloLensTextRecognitionMain::OnCameraAdded(
    HolographicSpace^ sender,
    HolographicSpaceCameraAddedEventArgs^ args
    )
{
    Deferral^ deferral = args->GetDeferral();
    HolographicCamera^ holographicCamera = args->Camera;
    create_task([this, deferral, holographicCamera] ()
    {
        //
        // TODO: Allocate resources for the new camera and load any content specific to
        //       that camera. Note that the render target size (in pixels) is a property
        //       of the HolographicCamera object, and can be used to create off-screen
        //       render targets that match the resolution of the HolographicCamera.
        //

        // Create device-based resources for the holographic camera and add it to the list of
        // cameras used for updates and rendering. Notes:
        //   * Since this function may be called at any time, the AddHolographicCamera function
        //     waits until it can get a lock on the set of holographic camera resources before
        //     adding the new camera. At 60 frames per second this wait should not take long.
        //   * A subsequent Update will take the back buffer from the RenderingParameters of this
        //     camera's CameraPose and use it to create the ID3D11RenderTargetView for this camera.
        //     Content can then be rendered for the HolographicCamera.
        m_deviceResources->AddHolographicCamera(holographicCamera);

        // Holographic frame predictions will not include any information about this camera until
        // the deferral is completed.
        deferral->Complete();
    });
}

void HoloLensTextRecognitionMain::OnCameraRemoved(
    HolographicSpace^ sender,
    HolographicSpaceCameraRemovedEventArgs^ args
    )
{
    create_task([this]()
    {
        //
        // TODO: Asynchronously unload or deactivate content resources (not back buffer 
        //       resources) that are specific only to the camera that was removed.
        //
    });

    // Before letting this callback return, ensure that all references to the back buffer 
    // are released.
    // Since this function may be called at any time, the RemoveHolographicCamera function
    // waits until it can get a lock on the set of holographic camera resources before
    // deallocating resources for this camera. At 60 frames per second this wait should
    // not take long.
    m_deviceResources->RemoveHolographicCamera(args->Camera);
}

//Adding voice recognition
// Change the cube color, if we get a valid result.
void HoloLensTextRecognitionMain::OnResultGenerated(SpeechContinuousRecognitionSession ^sender, SpeechContinuousRecognitionResultGeneratedEventArgs ^args)
{
	// For our list of commands, medium confidence is good enough. 
	// We also accept results that have high confidence.
	if ((args->Result->Confidence == SpeechRecognitionConfidence::High) ||
		(args->Result->Confidence == SpeechRecognitionConfidence::Medium))
	{
		m_lastCommand = args->Result->Text;

		// When the debugger is attached, we can print information to the debug console.
		PrintWstringToDebugConsole(
			std::wstring(L"Last command was: ") +
			m_lastCommand->Data() +
			L"\n"
		);

		// Play a sound to indicate a command was understood.
		PlayRecognitionSound();
	}
	else
	{
		OutputDebugStringW(L"Recognition confidence not high enough.\n");
	}
}

void HoloLensTextRecognitionMain::OnSpeechQualityDegraded(Windows::Media::SpeechRecognition::SpeechRecognizer^ recognizer, Windows::Media::SpeechRecognition::SpeechRecognitionQualityDegradingEventArgs^ args)
{
	switch (args->Problem)
	{
	case SpeechRecognitionAudioProblem::TooFast:
		OutputDebugStringW(L"The user spoke too quickly.\n");
		break;

	case SpeechRecognitionAudioProblem::TooSlow:
		OutputDebugStringW(L"The user spoke too slowly.\n");
		break;

	case SpeechRecognitionAudioProblem::TooQuiet:
		OutputDebugStringW(L"The user spoke too softly.\n");
		break;

	case SpeechRecognitionAudioProblem::TooLoud:
		OutputDebugStringW(L"The user spoke too loudly.\n");
		break;

	case SpeechRecognitionAudioProblem::TooNoisy:
		OutputDebugStringW(L"There is too much noise in the signal.\n");
		break;

	case SpeechRecognitionAudioProblem::NoSignal:
		OutputDebugStringW(L"There is no signal.\n");
		break;

	case SpeechRecognitionAudioProblem::None:
	default:
		OutputDebugStringW(L"An error was reported with no information.\n");
		break;
	}
}