/*=====================================================================
WebViewData.cpp
---------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#include "WebViewData.h"


#include "MainWindow.h"
#include "../shared/WorldObject.h"
#include "../qt/QtUtils.h"
#include <Escaping.h>
#include <FileInStream.h>
#include <PlatformUtils.h>
#include "../audio/AudioEngine.h"
#include <QtGui/QPainter>

#if defined(_WIN32) || defined(OSX)
#define CEF_SUPPORT 1
#endif

#if CEF_SUPPORT
#include <cef_app.h>
#include <cef_client.h>
#include <wrapper/cef_helpers.h>
#endif


static bool CEF_initialised = false;
static WebViewDataCEFApp* app = NULL; // Shared among all WebViewData objects.


#if CEF_SUPPORT


// This class is shared among all browser instances, the browser the callback applies to is passed in as arg 0.
class RenderHandler : public CefRenderHandler
{
public:
	RenderHandler(Reference<OpenGLTexture> opengl_tex_) : opengl_tex(opengl_tex_) {}

	~RenderHandler() {}

	void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override
	{
		CEF_REQUIRE_UI_THREAD();

		rect = CefRect(0, 0, (int)opengl_tex->xRes(), (int)opengl_tex->yRes());
	}

	// "|buffer| will be |width|*|height|*4 bytes in size and represents a BGRA image with an upper-left origin"
	void OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type, const RectList& dirty_rects, const void* buffer, int width, int height) override
	{
		CEF_REQUIRE_UI_THREAD();

		//conPrint("OnPaint()");

		// whole page was updated
		if(type == PET_VIEW)
		{
			const bool ob_visible = opengl_engine->isObjectInCameraFrustum(*ob->opengl_engine_ob);
			if(ob_visible)
			{
				for(size_t i=0; i<dirty_rects.size(); ++i)
				{
					const CefRect& rect = dirty_rects[i];

					//conPrint("Updating dirty rect " + toString(rect.x) + ", " + toString(rect.y) + ", w: " + toString(rect.width) + ", h: " + toString(rect.height));

					// Copy dirty rect data into a packed buffer

					const uint8* start_px = (uint8*)buffer + (width * 4) * rect.y + 4 * rect.x;
					opengl_tex->loadRegionIntoExistingTexture(rect.x, rect.y, rect.width, rect.height, /*row stride (B) = */width * 4, ArrayRef<uint8>(start_px, rect.width * rect.height * 4));
				}
			}


			// if there is still a popup open, write it into the page too (it's pixels will have been
			// copied into it's buffer by a call to OnPaint with type of PET_POPUP earlier)
			//if(gPopupPixels != nullptr)
			//{
			//    unsigned char* dst = gPagePixels + gPopupRect.y * gWidth * gDepth + gPopupRect.x * gDepth;
			//    unsigned char* src = (unsigned char*)gPopupPixels;
			//    while(src < (unsigned char*)gPopupPixels + gPopupRect.width * gPopupRect.height * gDepth)
			//    {
			//        memcpy(dst, src, gPopupRect.width * gDepth);
			//        src += gPopupRect.width * gDepth;
			//        dst += gWidth * gDepth;
			//    }
			//}

		}
		// popup was updated
		else if(type == PET_POPUP)
		{
			//std::cout << "OnPaint() for popup: " << width << " x " << height << " at " << gPopupRect.x << " x " << gPopupRect.y << std::endl;
		
			// copy over the popup pixels into it's buffer
			// (popup buffer created in onPopupSize() as we know the size there)
			//memcpy(gPopupPixels, buffer, width * height * gDepth);
			//
			//// copy over popup pixels into page pixels. We need this for when popup is changing (e.g. highlighting or scrolling)
			//// when the containing page is not changing and therefore doesn't get an OnPaint update
			//unsigned char* src = (unsigned char*)gPopupPixels;
			//unsigned char* dst = gPagePixels + gPopupRect.y * gWidth * gDepth + gPopupRect.x * gDepth;
			//while(src < (unsigned char*)gPopupPixels + gPopupRect.width * gPopupRect.height * gDepth)
			//{
			//    memcpy(dst, src, gPopupRect.width * gDepth);
			//    src += gPopupRect.width * gDepth;
			//    dst += gWidth * gDepth;
			//}
		}
	}

	void OnPopupShow(CefRefPtr<CefBrowser> browser, bool show) override
	{
		CEF_REQUIRE_UI_THREAD();
		//std::cout << "CefRenderHandler::OnPopupShow(" << (show ? "true" : "false") << ")" << std::endl;

		if(!show)
		{
			//delete gPopupPixels;
			//gPopupPixels = nullptr;
			//gPopupRect.Reset();
		}
	}

	void OnPopupSize(CefRefPtr<CefBrowser> browser, const CefRect& rect) override
	{
		CEF_REQUIRE_UI_THREAD();
		//std::cout << "CefRenderHandler::OnPopupSize(" << rect.width << " x " << rect.height << ") at " << rect.x << ", " << rect.y << std::endl;

		//gPopupRect = rect;

		/*if(gPopupPixels == nullptr)
		{
			gPopupPixels = new unsigned char[rect.width * rect.height * gDepth];
		}*/
	}

	Reference<OpenGLTexture> opengl_tex;
	OpenGLEngine* opengl_engine;
	WorldObject* ob;

	IMPLEMENT_REFCOUNTING(RenderHandler);
};


// This class is shared among all browser instances, the browser the callback applies to is passed in as arg 0.
class LifeSpanHandler : public CefLifeSpanHandler
{
public:
	LifeSpanHandler()
	{
	}
	virtual ~LifeSpanHandler()
	{
	}

	virtual bool OnBeforePopup(CefRefPtr<CefBrowser> browser,
		CefRefPtr<CefFrame> frame,
		const CefString& target_url,
		const CefString& target_frame_name,
		WindowOpenDisposition target_disposition,
		bool user_gesture,
		const CefPopupFeatures& popupFeatures,
		CefWindowInfo& windowInfo,
		CefRefPtr<CefClient>& client,
		CefBrowserSettings& settings,
		CefRefPtr<CefDictionaryValue>& extra_info,
		bool* no_javascript_access) override
	{
		CEF_REQUIRE_UI_THREAD();

		conPrint("Page wants to open a popup: " + std::string(target_url));

		// If this was an explicit click on a link, just visit the popup link directly, since we don't want to open in a new tab or window.
		if(user_gesture) // user_gesture is true if the popup was opened via explicit user gesture.
		{
			if(browser && browser->GetHost())
			{
				browser->GetMainFrame()->LoadURL(target_url);
			}
		}

		return true; // "To cancel creation of the popup browser return true"
	}

	void OnAfterCreated(CefRefPtr<CefBrowser> browser) override
	{
		CEF_REQUIRE_UI_THREAD();

		mBrowserList.push_back(browser);
	}

	void OnBeforeClose(CefRefPtr<CefBrowser> browser) override
	{
		CEF_REQUIRE_UI_THREAD();

		BrowserList::iterator bit = mBrowserList.begin();
		for(; bit != mBrowserList.end(); ++bit)
		{
			if((*bit)->IsSame(browser))
			{
				mBrowserList.erase(bit);
				break;
			}
		}
	}

	IMPLEMENT_REFCOUNTING(LifeSpanHandler);

public:
	typedef std::list<CefRefPtr<CefBrowser>> BrowserList;
	BrowserList mBrowserList;
};


class WebDataCefClient : public CefClient, public CefRequestHandler, public CefLoadHandler, public CefDisplayHandler, public CefAudioHandler
{
public:
	WebDataCefClient() : num_channels(0), sample_rate(0) {}

	CefRefPtr<CefRenderHandler> GetRenderHandler() override
	{
		return mRenderHandler;
	}

	CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override
	{
		return mLifeSpanHandler;
	}

	CefRefPtr<CefRequestHandler> GetRequestHandler() override
	{
		return this;
	}

	CefRefPtr<CefDisplayHandler> GetDisplayHandler() override
	{
		return this;
	}

	CefRefPtr<CefLoadHandler> GetLoadHandler() override
	{
		return this;
	}

	CefRefPtr<CefAudioHandler> GetAudioHandler() override
	{
		return this;
	}

	virtual bool OnCursorChange(CefRefPtr<CefBrowser> browser,
		CefCursorHandle cursor,
		cef_cursor_type_t type,
		const CefCursorInfo& custom_cursor_info) override
	{
		return false;
	}

	bool OnOpenURLFromTab(CefRefPtr<CefBrowser> browser,
		CefRefPtr<CefFrame> frame,
		const CefString& target_url,
		CefRequestHandler::WindowOpenDisposition target_disposition,
		bool user_gesture) override
	{
		conPrint("OnOpenURLFromTab is called");

		return true;
	}

	bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
		CefRefPtr<CefFrame> frame,
		CefRefPtr<CefRequest> request,
		bool user_gesture,
		bool is_redirect) override

	{
		std::string frame_name = frame->GetName();
		std::string frame_url = frame->GetURL();

		conPrint("OnBeforeBrowse  is called: ");
		conPrint("                  Name is: " + std::string(frame_name));
		conPrint("                   URL is: " + std::string(frame_url));

		return false;
	}



	void OnLoadStart(CefRefPtr<CefBrowser> browser,
		CefRefPtr<CefFrame> frame,
		TransitionType transition_type) override
	{
		CEF_REQUIRE_UI_THREAD();

		if(frame->IsMain())
		{
			conPrint("Loading started");
		}
	}

	void OnLoadEnd(CefRefPtr<CefBrowser> browser,
		CefRefPtr<CefFrame> frame,
		int httpStatusCode) override
	{
		CEF_REQUIRE_UI_THREAD();

		if(frame->IsMain())
		{
			const std::string url = frame->GetURL();

			conPrint("Load ended for URL: " + std::string(url) + " with HTTP status code: " + toString(httpStatusCode));
		}
	}


	//--------------------- AudioHandler ----------------------------
	// Called on the UI thread to allow configuration of audio stream parameters.
	// Return true to proceed with audio stream capture, or false to cancel it.
	// All members of |params| can optionally be configured here, but they are
	// also pre-filled with some sensible defaults.
	virtual bool GetAudioParameters(CefRefPtr<CefBrowser> browser,
		CefAudioParameters& params) override 
	{
		params.sample_rate = main_window->audio_engine.getSampleRate();
		return true;
	}

	// Called on a browser audio capture thread when the browser starts
	// streaming audio.
	virtual void OnAudioStreamStarted(CefRefPtr<CefBrowser> browser,
		const CefAudioParameters& params,
		int channels) override
	{
		// Create audio source
		if(ob->audio_source.isNull())
		{
			// conPrint("OnAudioStreamStarted(), creating audio src");

			// Create a streaming audio source.
			glare::AudioSourceRef audio_source = new glare::AudioSource();
			audio_source->type = glare::AudioSource::SourceType_Streaming;
			audio_source->pos = ob->aabb_ws.centroid();
			audio_source->debugname = ob->target_url;

			{
				Lock lock(main_window->world_state->mutex);

				const Parcel* parcel = main_window->world_state->getParcelPointIsIn(ob->pos);
				audio_source->userdata_1 = parcel ? parcel->id.value() : ParcelID::invalidParcelID().value(); // Save the ID of the parcel the object is in, in userdata_1 field of the audio source.
			}

			ob->audio_source = audio_source;
			main_window->audio_engine.addSource(audio_source);
		}

		sample_rate = params.sample_rate;
		num_channels = channels;
	}

	// Called on the audio stream thread when a PCM packet is received for the
	// stream. |data| is an array representing the raw PCM data as a floating
	// point type, i.e. 4-byte value(s). |frames| is the number of frames in the
	// PCM packet. |pts| is the presentation timestamp (in milliseconds since the
	// Unix Epoch) and represents the time at which the decompressed packet should
	// be presented to the user. Based on |frames| and the |channel_layout| value
	// passed to OnAudioStreamStarted you can calculate the size of the |data|
	// array in bytes.
	virtual void OnAudioStreamPacket(CefRefPtr<CefBrowser> browser,
		const float** data,
		int frames,
		int64 pts) override
	{
		// Copy to our audio source
		if(ob->audio_source.nonNull())
		{
			const int num_samples = frames;
			temp_buf.resize(num_samples);

			if(num_channels == 1)
			{
				const float* const data0 = data[0];
				for(int z=0; z<num_samples; ++z)
					temp_buf[z] =  data0[z];
			}
			else if(num_channels == 2)
			{
				const float* const data0 = data[0];
				const float* const data1 = data[1];

				for(int z=0; z<num_samples; ++z)
				{
					const float left =  data0[z];
					const float right = data1[z];
					temp_buf[z] = (left + right) * 0.5f;
				}
			}
			else
			{
				for(int z=0; z<num_samples; ++z)
					temp_buf[z] = 0.f;
			}


			{
				Lock mutex(main_window->audio_engine.mutex);
				ob->audio_source->buffer.pushBackNItems(temp_buf.data(), num_samples);
			}
		}
	}

	virtual void OnAudioStreamStopped(CefRefPtr<CefBrowser> browser) override
	{
		if(ob && ob->audio_source.nonNull())
		{
			main_window->audio_engine.removeSource(ob->audio_source);
			ob->audio_source = NULL;
		}
	}

	virtual void OnAudioStreamError(CefRefPtr<CefBrowser> browser, const CefString& message) override
	{
		conPrint("=========================================\nOnAudioStreamError: " + message.ToString());
	}
	// ----------------------------------------------------------------------------


	CefRefPtr<RenderHandler> mRenderHandler;
	CefRefPtr<CefLifeSpanHandler> mLifeSpanHandler; // The lifespan handler has references to the CefBrowsers, so the browsers should 

	MainWindow* main_window;
	WorldObject* ob;
	int num_channels;
	int sample_rate;

	std::vector<float> temp_buf;

	IMPLEMENT_REFCOUNTING(WebDataCefClient);
};


class WebViewCEFBrowser : public RefCounted
{
public:
	WebViewCEFBrowser(WebViewData* web_view_data_, RenderHandler* render_handler, LifeSpanHandler* lifespan_handler)
	:	web_view_data(web_view_data_),
		mRenderHandler(render_handler),
		mLifeSpanHandler(lifespan_handler)
	{
		cef_client = new WebDataCefClient();
		cef_client->mRenderHandler = mRenderHandler;
		cef_client->mLifeSpanHandler = lifespan_handler;
	}

	virtual ~WebViewCEFBrowser()
	{
	}

	virtual void OnStatusMessage(CefRefPtr<CefBrowser> browser,
		const CefString& value)
	{
		if(value.c_str())
		{
			//conPrint("OnStatusMessage: " + StringUtils::PlatformToUTF8UnicodeEncoding(value.c_str()));

			web_view_data->linkHoveredSignal(QtUtils::toQString(value.ToString()));
		}
		else
		{
			//conPrint("OnStatusMessage: NULL");

			web_view_data->linkHoveredSignal("");
		}
	}

	void sendMouseClickEvent(CefBrowserHost::MouseButtonType btn_type, float uv_x, float uv_y, bool mouse_up, uint32 cef_modifiers)
	{
		if(cef_browser && cef_browser->GetHost())
		{
			//mBrowser->GetHost()->SendFocusEvent(true);

			CefMouseEvent cef_mouse_event;
			cef_mouse_event.Reset();
			cef_mouse_event.x = uv_x         * mRenderHandler->opengl_tex->xRes();
			cef_mouse_event.y = (1.f - uv_y) * mRenderHandler->opengl_tex->yRes();
			cef_mouse_event.modifiers = cef_modifiers;

			int last_click_count = 1;
			cef_browser->GetHost()->SendMouseClickEvent(cef_mouse_event, btn_type, mouse_up, last_click_count);
		}
	}

	void sendMouseMoveEvent(float uv_x, float uv_y, uint32 cef_modifiers)
	{
		if(cef_browser && cef_browser->GetHost())
		{
			CefMouseEvent cef_mouse_event;
			cef_mouse_event.Reset();
			cef_mouse_event.x = uv_x         * mRenderHandler->opengl_tex->xRes();
			cef_mouse_event.y = (1.f - uv_y) * mRenderHandler->opengl_tex->yRes();
			cef_mouse_event.modifiers = cef_modifiers;

			bool mouse_leave = false;
			cef_browser->GetHost()->SendMouseMoveEvent(cef_mouse_event, mouse_leave);
		}
	}

	void sendMouseWheelEvent(float uv_x, float uv_y, int delta_x, int delta_y, uint32 cef_modifiers)
	{
		if(cef_browser && cef_browser->GetHost())
		{
			CefMouseEvent cef_mouse_event;
			cef_mouse_event.Reset();
			cef_mouse_event.x = uv_x         * mRenderHandler->opengl_tex->xRes();
			cef_mouse_event.y = (1.f - uv_y) * mRenderHandler->opengl_tex->yRes();
			cef_mouse_event.modifiers = cef_modifiers;

			cef_browser->GetHost()->SendMouseWheelEvent(cef_mouse_event, (int)delta_x, (int)delta_y);
		}
	}

	void sendKeyEvent(cef_key_event_type_t event_type, int key, int native_virtual_key, uint32 cef_modifiers)
	{
		if(cef_browser && cef_browser->GetHost())
		{
			//mBrowser->GetHost()->SetFocus(true);

			CefKeyEvent key_event;
			key_event.Reset();
			key_event.type = event_type;
			//key_event.character = (char16)key;
			//key_event.unmodified_character = (char16)key;

			if(event_type == KEYEVENT_CHAR) // key >= 'a' && key <= 'z')
			{
				key_event.character = (char16)key;
				key_event.unmodified_character = (char16)key;

				key_event.windows_key_code = (char16)key; // TEMP native_virtual_key; // ??
				key_event.native_key_code = (char16)key; // TEMP native_virtual_key; // ??
			}
			else
			{
				key_event.windows_key_code = native_virtual_key; // ??
				key_event.native_key_code = native_virtual_key; // ??
			}
			key_event.modifiers = cef_modifiers;
			//key_event.focus_on_editable_field = true;//TEMP
			cef_browser->GetHost()->SendKeyEvent(key_event);
		}
	}

	void sendBackMousePress()
	{
		if(cef_browser)
			cef_browser->GoBack();
	}

	void sendForwardsMousePress()
	{
		if(cef_browser)
			cef_browser->GoForward();
	}

	void navigate(const std::string& url)
	{
		if(cef_browser && cef_browser->GetHost())
		{
			cef_browser->GetMainFrame()->LoadURL(url);
		}
	}

	void requestExit()
	{
		if(cef_browser.get() && cef_browser->GetHost())
		{
			cef_browser->GetHost()->CloseBrowser(/*force_close=*/true);
		}
	}

	WebViewData* web_view_data;
	MainWindow* main_window;
	WorldObject* ob;

	CefRefPtr<RenderHandler> mRenderHandler;
	CefRefPtr<CefBrowser> cef_browser;
	CefRefPtr<WebDataCefClient> cef_client;

	CefRefPtr<CefLifeSpanHandler> mLifeSpanHandler;
};


class WebViewDataCEFApp : public CefApp, public RefCounted
{
public:
	WebViewDataCEFApp()
	{
		lifespan_handler = new LifeSpanHandler();
	}

	~WebViewDataCEFApp()
	{
		if(CEF_initialised)
			shutdownCEF();
	}

	void initialise(const std::string& base_dir_path)
	{
		// Initialise CEF if not already done so.
		if(!CEF_initialised)
			initialiseCEF(base_dir_path);
	}

	void initialiseCEF(const std::string& base_dir_path)
	{
		assert(!CEF_initialised);

		CefMainArgs args;// (GetModuleHandle(NULL));

		CefSettings settings;
		settings.no_sandbox = true;
		const std::string browser_process_path = base_dir_path + "/browser_process.exe";
		CefString(&settings.browser_subprocess_path).FromString(browser_process_path);

		bool result = CefInitialize(args, settings, this, /*windows sandbox info=*/NULL);
		if(result)
			CEF_initialised = true;
		else
		{
			conPrint("CefInitialize failed.");
		}
	}

	void shutdownCEF()
	{
		assert(CEF_initialised);
		
		// Wait until browser processes are shut down
		while(!lifespan_handler->mBrowserList.empty())
		{
			PlatformUtils::Sleep(1);
			CefDoMessageLoopWork();
		}

		lifespan_handler = CefRefPtr<LifeSpanHandler>();

		CEF_initialised = false;
		CefShutdown();
	}


	Reference<WebViewCEFBrowser> createBrowser(WebViewData* web_view_data, const std::string& URL, Reference<OpenGLTexture> opengl_tex)
	{
		Reference<WebViewCEFBrowser> browser = new WebViewCEFBrowser(web_view_data, new RenderHandler(opengl_tex), lifespan_handler.get());

		CefWindowInfo window_info;
		window_info.windowless_rendering_enabled = true;

		CefBrowserSettings browser_settings;
		browser_settings.windowless_frame_rate = 60;
		browser_settings.background_color = CefColorSetARGB(255, 100, 100, 100);

		browser->cef_browser = CefBrowserHost::CreateBrowserSync(window_info, browser->cef_client, CefString(URL), browser_settings, nullptr, nullptr);

		// There is a brief period before the audio capture kicks in, resulting in a burst of loud sound.  We can work around this by setting to mute here.
		// See https://bitbucket.org/chromiumembedded/cef/issues/3319/burst-of-uncaptured-audio-after-creating
		browser->cef_browser->GetHost()->SetAudioMuted(true);
		return browser;
	}

	void OnBeforeCommandLineProcessing(const CefString& process_type, CefRefPtr<CefCommandLine> command_line) override
	{
		// To allow autoplaying videos etc. without having to click:
		// See https://www.magpcss.org/ceforum/viewtopic.php?f=6&t=16517
		command_line->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");

		/*if(process_type.empty())
		{
			command_line->AppendSwitch("disable-gpu");
			command_line->AppendSwitch("disable-gpu-compositing");
		}*/
	}

	
	IMPLEMENT_REFCOUNTING(WebViewDataCEFApp);

public:

	CefRefPtr<LifeSpanHandler> lifespan_handler;
};

#else // else if !CEF_SUPPORT: 

class WebViewCEFBrowser : public RefCounted
{};

#endif // CEF_SUPPORT


WebViewData::WebViewData()
:	cur_load_progress(0),
	loading_in_progress(false),
	browser(NULL),
	showing_click_to_load_text(false),
	user_clicked_to_load(false)
{

}


WebViewData::~WebViewData()
{
#if CEF_SUPPORT
	if(browser.nonNull())
	{
		browser->requestExit();
		browser = NULL;
	}
#endif
}


void WebViewData::doMessageLoopWork()
{
#if CEF_SUPPORT
	if(CEF_initialised)
		CefDoMessageLoopWork();
#endif
}


void WebViewData::shutdownCEF()
{
	//conPrint("===========================WebViewData::shutdownCEF()===========================");
#if CEF_SUPPORT
	if(app)
	{
		delete app;
		app = NULL;
	}
#endif
	//conPrint("===========================WebViewData::shutdownCEF() done===========================");
}


static const int text_tex_W = 512;
static const int button_W = 200;
static const int button_left_x = text_tex_W/2 - button_W/2;
static const int button_top_y = (int)((1080.0 / 1920) * text_tex_W) - 120;
static const int button_H = 60;

static OpenGLTextureRef makeTextTexture(OpenGLEngine* opengl_engine, const std::string& text)
{
	const int W = text_tex_W;
	const int H = (int)((1080.0 / 1920) * W);

	QImage qimage(W, H, QImage::Format_RGBA8888); // The 32 bit Qt formats seem faster than the 24 bit formats.
	qimage.fill(QColor(220, 220, 220));
	QPainter painter(&qimage);
	painter.setPen(QPen(QColor(30, 30, 30)));
	painter.setFont(QFont("helvetica", 20, QFont::Normal));
	const int padding = 20;
	painter.drawText(QRect(padding, padding, W - padding*2, H - padding*2), Qt::TextWordWrap | Qt::AlignLeft, QtUtils::toQString(text));

	painter.drawRect(W/2 - 100, button_top_y, 200, button_H);

	//painter.setPen(QPen(QColor(30, 30, 30)));
	//painter.setFont(QFont("helvetica", 20, QFont::Normal));
	painter.drawText(QRect(button_left_x, button_top_y, /*width=*/button_W, /*height=*/button_H), Qt::AlignVCenter | Qt::AlignHCenter, "Load"); // y=0 at top

	OpenGLTextureRef tex = new OpenGLTexture(W, H, opengl_engine, ArrayRef<uint8>(NULL, 0), OpenGLTexture::Format_SRGBA_Uint8, OpenGLTexture::Filtering_Bilinear);
	tex->loadIntoExistingTexture(W, H, /*row stride B=*/qimage.bytesPerLine(), ArrayRef<uint8>(qimage.constBits(), qimage.sizeInBytes()));
	return tex;
}


static bool uvsAreOnLoadButton(float uv_x, float uv_y)
{
	const int W = text_tex_W;
	const int H = (int)((1080.0 / 1920) * W);
	const int x = (int)(uv_x * W);
	const int y = (int)((1.f - uv_y) * H);
	
	return
		x >= button_left_x && x <= (button_left_x + button_W) &&
		y >= button_top_y && y <= (button_top_y + button_H);
}


void WebViewData::process(MainWindow* main_window, OpenGLEngine* opengl_engine, WorldObject* ob, double anim_time, double dt)
{
#if CEF_SUPPORT
	/*const int width = 1920;
	const int height = 1080;*/
	const int width = 1024;
	const int height = (int)(1024.f / 1920.f * 1080.f); // Webview uses 1920 : 1080 aspect ratio.  (See MainWindow::on_actionAdd_Web_View_triggered())

	const double ob_dist_from_cam = ob->pos.getDist(main_window->cam_controller.getPosition());
	const double max_play_dist = maxBrowserDist();
	const bool in_process_dist = ob_dist_from_cam < max_play_dist;
	if(in_process_dist)
	{
		if(!app)
		{
			app = new WebViewDataCEFApp();
			app->AddRef(); // Since we are just storing a pointer to WebViewDataCEFApp, manually increment ref count.

			app->initialise(main_window->base_dir_path);
		}

		if(app)
		{
			if(browser.isNull() && !ob->target_url.empty() && ob->opengl_engine_ob.nonNull())
			{
				const bool URL_in_whitelist = main_window->world_state->url_whitelist.isURLPrefixInWhitelist(ob->target_url);
				if(user_clicked_to_load || URL_in_whitelist)
				{
					main_window->logMessage("Creating browser, target_url: " + ob->target_url);

					if(ob->opengl_engine_ob.nonNull())
					{
						ob->opengl_engine_ob->materials[0].albedo_texture = new OpenGLTexture(width, height, opengl_engine, ArrayRef<uint8>(NULL, 0), OpenGLTexture::Format_SRGBA_Uint8,
							GL_SRGB8_ALPHA8, // GL internal format
							GL_BGRA, // GL format.
							OpenGLTexture::Filtering_Bilinear);

						ob->opengl_engine_ob->materials[0].fresnel_scale = 0; // Remove specular reflections, reduces washed-out look.
					}

					browser = app->createBrowser(this, ob->target_url, ob->opengl_engine_ob->materials[0].albedo_texture);
					browser->main_window = main_window;
					browser->ob = ob;
					browser->cef_client->main_window = main_window;
					browser->cef_client->ob = ob;
					browser->mRenderHandler->opengl_engine = opengl_engine;
					browser->mRenderHandler->ob = ob;
				}
				else
				{
					if(ob->opengl_engine_ob->materials[0].albedo_texture.isNull())
					{
						assert(!showing_click_to_load_text);
						ob->opengl_engine_ob->materials[0].albedo_texture = makeTextTexture(opengl_engine, "Click below to load " + ob->target_url); // TEMP
						showing_click_to_load_text = true;
					}
				}

				this->loaded_target_url = ob->target_url;
			}

			// If target url has changed, tell webview to load it
			if(browser.nonNull() && (ob->target_url != this->loaded_target_url))
			{
				// conPrint("Webview loading URL '" + ob->target_url + "'...");

				const bool URL_in_whitelist = main_window->world_state->url_whitelist.isURLPrefixInWhitelist(ob->target_url);
				if(URL_in_whitelist)
				{
					browser->navigate(ob->target_url);

					this->loaded_target_url = ob->target_url;
				}
				else
				{
					main_window->logMessage("Closing browser (URL changed), target_url: " + ob->target_url);
					browser->requestExit();
					browser = NULL;

					// Remove audio source
					if(ob->audio_source.nonNull())
					{
						main_window->audio_engine.removeSource(ob->audio_source);
						ob->audio_source = NULL;
					}

					user_clicked_to_load = false;
					ob->opengl_engine_ob->materials[0].albedo_texture = makeTextTexture(opengl_engine, "Click below to load " + ob->target_url);
					showing_click_to_load_text = true;
				}

			}
		}
	}
	else // else if !in_process_dist:
	{
		if(browser.nonNull())
		{
			main_window->logMessage("Closing browser (out of view distance), target_url: " + ob->target_url);
			browser->requestExit();
			browser = NULL;

			// Remove audio source
			if(ob->audio_source.nonNull())
			{
				main_window->audio_engine.removeSource(ob->audio_source);
				ob->audio_source = NULL;
			}
		}
	}
#endif // CEF_SUPPORT

#if 0
	{
		if(time_since_last_webview_display.elapsed() > 0.0333) // Max 30 fps
		{
			if(webview_qimage.size() != webview->size())
			{
				webview_qimage = QImage(webview->size(), QImage::Format_RGBA8888); // The 32 bit Qt formats seem faster than the 24 bit formats.
			}

			const int W = webview_qimage.width();
			const int H = webview_qimage.height();

			QPainter painter(&webview_qimage);

			// Draw the web-view to the QImage
			webview->render(&painter);

			// Draw hovered-over link URL at bottom left
			if(!this->current_hovered_URL.isEmpty())
			{
				QFont system_font = QApplication::font();
				system_font.setPointSize(16);

				QFontMetrics metrics(system_font);
				QRect text_rect = metrics.boundingRect(this->current_hovered_URL);

				//printVar(text_rect.x());
				//printVar(text_rect.y());
				//printVar(text_rect.top());
				//printVar(text_rect.bottom());
				//printVar(text_rect.left());
				//printVar(text_rect.right());
				/*
				For example:
				text_rect.x(): 0
				text_rect.y(): -23
				text_rect.top(): -23
				text_rect.bottom(): 4
				text_rect.left(): 0
				text_rect.right(): 464
				*/
				const int x_padding = 12; // in pixels
				const int y_padding = 12; // in pixels
				const int link_W = text_rect.width()  + x_padding*2;
				const int link_H = -text_rect.top() + y_padding*2;

				painter.setPen(QPen(QColor(200, 200, 200)));
				painter.fillRect(0, H - link_H, link_W, link_H, QBrush(QColor(200, 200, 200), Qt::SolidPattern));

				painter.setPen(QPen(QColor(0, 0, 0)));
				painter.setFont(system_font);
				painter.drawText(QPoint(x_padding, /*font baseline y=*/H - y_padding), this->current_hovered_URL);
			}

			// Draw loading indicator
			if(loading_in_progress)
			{
				const int loading_bar_w = (int)((float)W * cur_load_progress / 100.f);
				const int loading_bar_h = 8;
				painter.fillRect(0, H - loading_bar_h, loading_bar_w, loading_bar_h, QBrush(QColor(100, 100, 255), Qt::SolidPattern));
			}

			painter.end();

			if(ob->opengl_engine_ob->materials[0].albedo_texture.isNull())
			{
				ob->opengl_engine_ob->materials[0].albedo_texture = new OpenGLTexture(W, H, opengl_engine, OpenGLTexture::Format_SRGBA_Uint8, OpenGLTexture::Filtering_Bilinear);
			}

			// Update texture
			ob->opengl_engine_ob->materials[0].albedo_texture->load(W, H, /*row stride B=*/webview_qimage.bytesPerLine(), ArrayRef<uint8>(webview_qimage.constBits(), webview_qimage.sizeInBytes()));


			//webview_qimage.save("webview_qimage.png", "png");

			time_since_last_webview_display.reset();
		}
	}
#endif
}


#if CEF_SUPPORT


static CefBrowserHost::MouseButtonType convertToCEFMouseButton(Qt::MouseButton button)
{
	if(button == Qt::LeftButton)
		return MBT_LEFT;
	else if(button == Qt::RightButton)
		return MBT_RIGHT;
	else if(button == Qt::MiddleButton)
		return MBT_MIDDLE;
	else
		return MBT_LEFT;
}


static uint32 convertToCEFModifiers(Qt::KeyboardModifiers modifiers)
{
	uint32 m = 0;
	if(modifiers.testFlag(Qt::ShiftModifier))	m |= EVENTFLAG_SHIFT_DOWN;
	if(modifiers.testFlag(Qt::ControlModifier)) m |= EVENTFLAG_CONTROL_DOWN;
	if(modifiers.testFlag(Qt::AltModifier))		m |= EVENTFLAG_ALT_DOWN;
	return m;
}


static uint32 convertToCEFModifiers(Qt::KeyboardModifiers modifiers, Qt::MouseButtons mouse_buttons)
{
	uint32 m = convertToCEFModifiers(modifiers);

	if(mouse_buttons.testFlag(Qt::LeftButton))	m |= EVENTFLAG_LEFT_MOUSE_BUTTON;
	if(mouse_buttons.testFlag(Qt::RightButton))	m |= EVENTFLAG_RIGHT_MOUSE_BUTTON;
	return m;
}


#endif // CEF_SUPPORT


void WebViewData::mouseReleased(QMouseEvent* e, const Vec2f& uv_coords)
{
	//conPrint("mouseReleased()");
#if CEF_SUPPORT
	if(browser.nonNull())
	{
		if(e->button() == Qt::BackButton) // bottom thumb button.  Not a CEF mouse button option, so handle explicitly.  Nothing to do for mouse-up
		{}
		else if(e->button() == Qt::ForwardButton) // top thumb button.  Nothing to do for mouse-up
		{}
		else
		{
			browser->sendMouseClickEvent(convertToCEFMouseButton(e->button()), uv_coords.x, uv_coords.y, /*mouse_up=*/true, convertToCEFModifiers(e->modifiers(), e->buttons()));
		}
	}
#endif
}


void WebViewData::mousePressed(QMouseEvent* e, const Vec2f& uv_coords)
{
	if(showing_click_to_load_text && uvsAreOnLoadButton(uv_coords.x, uv_coords.y))
		user_clicked_to_load = true;

	//conPrint("mousePressed()");
#if CEF_SUPPORT
	if(browser.nonNull())
	{
		if(e->button() == Qt::BackButton) // bottom thumb button.  Not a CEF mouse button option, so handle explicitly.
		{
			browser->sendBackMousePress();
		}
		else if(e->button() == Qt::ForwardButton) // top thumb button
		{
			browser->sendForwardsMousePress();
		}
		else
		{
			browser->sendMouseClickEvent(convertToCEFMouseButton(e->button()), uv_coords.x, uv_coords.y, /*mouse_up=*/false, convertToCEFModifiers(e->modifiers(), e->buttons()));
		}
	}
#endif
}


void WebViewData::mouseDoubleClicked(QMouseEvent* e, const Vec2f& uv_coords)
{
	//conPrint("mouseDoubleClicked()");
}


void WebViewData::mouseMoved(QMouseEvent* e, const Vec2f& uv_coords)
{
	//conPrint("mouseMoved(), uv_coords: " + uv_coords.toString());
#if CEF_SUPPORT
	if(browser.nonNull())
		browser->sendMouseMoveEvent(uv_coords.x, uv_coords.y, convertToCEFModifiers(e->modifiers(), e->buttons()));
#endif
}


void WebViewData::wheelEvent(QWheelEvent* e, const Vec2f& uv_coords)
{
	//conPrint("wheelEvent(), uv_coords: " + uv_coords.toString());
#if CEF_SUPPORT
	if(browser.nonNull())
		browser->sendMouseWheelEvent(uv_coords.x, uv_coords.y, e->angleDelta().x(), e->angleDelta().y(), convertToCEFModifiers(e->modifiers(), e->buttons()));
#endif
}


void WebViewData::keyPressed(QKeyEvent* e)
{
#if CEF_SUPPORT
	const uint32 modifiers = convertToCEFModifiers(e->modifiers());

	// This song and dance of sending two events seems to be needed to type both punctuation and alphabetic characters.

	if(browser.nonNull())
		browser->sendKeyEvent(KEYEVENT_RAWKEYDOWN, e->key(), e->nativeVirtualKey(), modifiers);

	if(!e->text().isEmpty())
	{
		//conPrint(QtUtils::toStdString(e->text()));
		if(browser.nonNull())
			browser->sendKeyEvent(KEYEVENT_CHAR, e->text().at(0).toLatin1(), e->nativeVirtualKey(), modifiers);
	}
#endif
}


void WebViewData::keyReleased(QKeyEvent* e)
{
#if CEF_SUPPORT
	const uint32 modifiers = convertToCEFModifiers(e->modifiers());

	if(browser.nonNull())
		browser->sendKeyEvent(KEYEVENT_KEYUP, e->key(), e->nativeVirtualKey(), modifiers);
#endif
}


void WebViewData::loadStartedSlot()
{
	//conPrint("loadStartedSlot()");
	loading_in_progress = true;
}


void WebViewData::loadProgress(int progress)
{
	//conPrint("loadProgress(): " + toString(progress));
	cur_load_progress = progress;
}


void WebViewData::loadFinished(bool ok)
{
	//conPrint("loadFinished(): " + boolToString(ok));
	loading_in_progress = false;
}


void WebViewData::linkHovered(const QString &url)
{
	//conPrint("linkHovered(): " + QtUtils::toStdString(url));
	this->current_hovered_URL = url;
}
