#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinder/gl/gl.h"
#include "cinder/gl/Fbo.h"

// Settings
#include "SDASettings.h"
// Session
#include "SDASession.h"
// Log
#include "SDALog.h"
// Spout
#include "CiSpoutIn.h"
// ndi
#include "CinderNDISender.h"
// warping
#include "Warp.h"

using namespace ci;
using namespace ci::app;
using namespace ph::warping;
using namespace std;
using namespace SophiaDigitalArt;

class SDAWarpingVizApp : public App {

public:
	SDAWarpingVizApp();
	void mouseMove(MouseEvent event) override;
	void mouseDown(MouseEvent event) override;
	void mouseDrag(MouseEvent event) override;
	void mouseUp(MouseEvent event) override;
	void keyDown(KeyEvent event) override;
	void keyUp(KeyEvent event) override;
	void fileDrop(FileDropEvent event) override;
	void update() override;
	void draw() override;
	void cleanup() override;
	void resize() override;
	void setUIVisibility(bool visible);
	typedef enum { IMAGE, SEQUENCE, SHADER, CAMERA, SHARED, AUDIO, STREAM } ModeType;

private:
	// Settings
	SDASettingsRef					mSDASettings;
	// Session
	SDASessionRef					mSDASession;
	// Log
	SDALogRef						mSDALog;
	ModeType						mMode;
	// Spout
	SpoutIn							mSpoutIn;
	gl::Texture2dRef				mSpoutTexture;
	// fbo
	bool							mIsShutDown;
	Anim<float>						mRenderWindowTimer;
	void							positionRenderWindow();
	bool							mFadeInDelay;
	bool							mFlipV;
	bool							mFlipH;
	int								xLeft, xRight, yLeft, yRight;
	int								margin, tWidth, tHeight;
	//! fbos
	void							renderToFbo();
	gl::FboRef						mFbo;
	//! shaders
	gl::GlslProgRef					mGlsl;
	// ndi
	CinderNDISender					mNDISender;
	ci::SurfaceRef 					mSurface;
	// warping
	bool			mUseBeginEnd;
	fs::path		mSettings;
	gl::TextureRef	mImage;
	WarpList		mWarps;
	Area			mSrcArea;
};


SDAWarpingVizApp::SDAWarpingVizApp()
	: mNDISender("WarpingViz")
{
	// Settings
	mSDASettings = SDASettings::create("WarpingViz");
	// Session
	mSDASession = SDASession::create(mSDASettings);
	mSDASession->getWindowsResolution();
	mMode = SHADER;
	// warping
	mUseBeginEnd = false;
	mSettings = getAssetPath("") / "warps.xml";
	if (fs::exists(mSettings)) {
		// load warp settings from file if one exists
		mWarps = Warp::readSettings(loadFile(mSettings));
	}
	else {
		// otherwise create a warp from scratch
		mWarps.push_back(WarpPerspectiveBilinear::create());
	}
	// load test image
	try {
		mImage = gl::Texture::create(loadImage(loadAsset("splash.jpg")),
			gl::Texture2d::Format().loadTopDown().mipmap(true).minFilter(GL_LINEAR_MIPMAP_LINEAR));

		mSrcArea = mImage->getBounds();

		// adjust the content size of the warps
		Warp::setSize(mWarps, mImage->getSize());
	}
	catch (const std::exception &e) {
		console() << e.what() << std::endl;
	}

	mFadeInDelay = true;
	mFlipV = false;
	mFlipH = true;
	xLeft = 0;
	xRight = mSDASettings->mRenderWidth;
	yLeft = 0;
	yRight = mSDASettings->mRenderHeight;
	margin = 20;
	tWidth = mSDASettings->mFboWidth / 2;
	tHeight = mSDASettings->mFboHeight / 2;
	// windows
	mIsShutDown = false;
	// fbo
	gl::Fbo::Format format;
	//format.setSamples( 4 ); // uncomment this to enable 4x antialiasing
	mFbo = gl::Fbo::create(mSDASettings->mRenderWidth, mSDASettings->mRenderHeight, format.depthTexture());
	// ndi
	mSurface = ci::Surface::create(mSDASettings->mRenderWidth, mSDASettings->mRenderHeight, true, SurfaceChannelOrder::BGRA);

	// shader
	mGlsl = gl::GlslProg::create(gl::GlslProg::Format().vertex(loadAsset("passthrough.vs")).fragment(loadAsset("post.glsl")));

	gl::enableDepthRead();
	gl::enableDepthWrite();
#ifdef _DEBUG

#else
	mRenderWindowTimer = 0.0f;
	timeline().apply(&mRenderWindowTimer, 1.0f, 2.0f).finishFn([&] { positionRenderWindow(); });
	positionRenderWindow();

#endif  // _DEBUG
}
void SDAWarpingVizApp::positionRenderWindow() {
	setUIVisibility(mSDASettings->mCursorVisible);
	mSDASettings->mRenderPosXY = ivec2(mSDASettings->mRenderX, mSDASettings->mRenderY);//20141214 was 0
	setWindowPos(mSDASettings->mRenderX, mSDASettings->mRenderY);
	setWindowSize(mSDASettings->mRenderWidth, mSDASettings->mRenderHeight);
}
void SDAWarpingVizApp::resize()
{
	// tell the warps our window has been resized, so they properly scale up or down
	Warp::handleResize(mWarps);
}
// Render into the FBO
void SDAWarpingVizApp::renderToFbo()
{

	// this will restore the old framebuffer binding when we leave this function
	// on non-OpenGL ES platforms, you can just call mFbo->unbindFramebuffer() at the end of the function
	// but this will restore the "screen" FBO on OpenGL ES, and does the right thing on both platforms
	gl::ScopedFramebuffer fbScp(mFbo);
	// clear out the FBO with black
	gl::clear(Color::black());

	// setup the viewport to match the dimensions of the FBO
	gl::ScopedViewport scpVp(ivec2(0), mFbo->getSize());

	// render

	// texture binding must be before ScopedGlslProg
	if (mSpoutTexture) {
		mSpoutTexture->bind(0);
	}
	else {
		mImage->bind(0);
	}
	gl::ScopedGlslProg prog(mGlsl);

	mGlsl->uniform("iGlobalTime", (float)getElapsedSeconds());
	mGlsl->uniform("iResolution", vec3(mSDASettings->mRenderWidth, mSDASettings->mRenderHeight, 1.0));
	mGlsl->uniform("iChannel0", 0); // texture 0
	mGlsl->uniform("iExposure", 1.0f);
	mGlsl->uniform("iSobel", 1.0f);
	mGlsl->uniform("iChromatic", 1.0f);

	gl::drawSolidRect(getWindowBounds());

}
void SDAWarpingVizApp::setUIVisibility(bool visible)
{
	if (visible)
	{
		showCursor();
	}
	else
	{
		hideCursor();
	}
}
void SDAWarpingVizApp::fileDrop(FileDropEvent event)
{
	mSDASession->fileDrop(event);
}
void SDAWarpingVizApp::update()
{
	if (mFadeInDelay == false) {
		mSDASession->setFloatUniformValueByIndex(mSDASettings->IFPS, getAverageFps());
		mSDASession->update();
		// render into our FBO
		if (mMode == SHADER) {
			renderToFbo();
		}
	}
}
void SDAWarpingVizApp::cleanup()
{
	if (!mIsShutDown)
	{
		mIsShutDown = true;
		CI_LOG_V("shutdown");
		// save settings
		// save warp settings
		Warp::writeSettings(mWarps, writeFile(mSettings));
		mSDASettings->save();
		mSDASession->save();
		quit();
	}
}
void SDAWarpingVizApp::mouseMove(MouseEvent event)
{
	// pass this mouse event to the warp editor first
	if (!Warp::handleMouseMove(mWarps, event)) {
		// let your application perform its mouseMove handling here
		if (!mSDASession->handleMouseMove(event)) {
		}
	}

}
void SDAWarpingVizApp::mouseDown(MouseEvent event)
{
	// pass this mouse event to the warp editor first
	if (!Warp::handleMouseDown(mWarps, event)) {
		// let your application perform its mouseDown handling here
		if (!mSDASession->handleMouseDown(event)) {
			if (event.isRightDown()) {
				// Select a sender
				// SpoutPanel.exe must be in the executable path
				mSpoutIn.getSpoutReceiver().SelectSenderPanel(); // DirectX 11 by default
			}
		}
	}
}
void SDAWarpingVizApp::mouseDrag(MouseEvent event)
{
	if (!Warp::handleMouseDrag(mWarps, event)) {
		if (!mSDASession->handleMouseDrag(event)) {
		}
	}
}
void SDAWarpingVizApp::mouseUp(MouseEvent event)
{
	if (!Warp::handleMouseUp(mWarps, event)) {
		if (!mSDASession->handleMouseUp(event)) {
			// let your application perform its mouseUp handling here
		}
	}
}

void SDAWarpingVizApp::keyDown(KeyEvent event)
{
#if defined( CINDER_COCOA )
	bool isModDown = event.isMetaDown();
#else // windows
	bool isModDown = event.isControlDown();
#endif
	bool isShiftDown = event.isShiftDown();

	CI_LOG_V("main keydown: " + toString(event.getCode()) + " ctrl: " + toString(isModDown) + " shift: " + toString(isShiftDown));

	// pass this key event to the warp editor first
	if (!Warp::handleKeyDown(mWarps, event)) {
		//if (!mSDASession->handleKeyDown(event)) {
		switch (event.getCode()) {
		case KeyEvent::KEY_KP_PLUS:
		case KeyEvent::KEY_DOLLAR:
		case KeyEvent::KEY_TAB:
		case KeyEvent::KEY_f:
			positionRenderWindow();
			break;
		case KeyEvent::KEY_a:
			mMode = SHADER;
			break;
		case KeyEvent::KEY_z:
			mMode = SHARED;
			break;
		case KeyEvent::KEY_e:
			mMode = IMAGE;
			break;
		case KeyEvent::KEY_v:
			mFlipV = !mFlipV;
			break;
		case KeyEvent::KEY_h:
			mFlipH = !mFlipH;
			break;
		case KeyEvent::KEY_ESCAPE:
			// quit the application
			quit();
			break;
		case KeyEvent::KEY_w:
			CI_LOG_V("wsConnect");
			if (isModDown) {
				mSDASession->wsConnect();
			}
			else {
				// toggle warp edit mode
				Warp::enableEditMode(!Warp::isEditModeEnabled());
			}
			break;
		case KeyEvent::KEY_r:
			// toggle drawing a random region of the image
			if (mSrcArea.getWidth() != mImage->getWidth() || mSrcArea.getHeight() != mImage->getHeight())
				mSrcArea = mImage->getBounds();
			else {
				int x1 = Rand::randInt(0, mImage->getWidth() - 150);
				int y1 = Rand::randInt(0, mImage->getHeight() - 150);
				int x2 = Rand::randInt(x1 + 150, mImage->getWidth());
				int y2 = Rand::randInt(y1 + 150, mImage->getHeight());
				mSrcArea = Area(x1, y1, x2, y2);
			}
			break;
		case KeyEvent::KEY_SPACE:
			// toggle drawing mode
			mUseBeginEnd = !mUseBeginEnd;
			break;
		case KeyEvent::KEY_c:
			// mouse cursor and ui visibility
			mSDASettings->mCursorVisible = !mSDASettings->mCursorVisible;
			setUIVisibility(mSDASettings->mCursorVisible);
			break;
		}
	}
	CI_LOG_V("key " + toString(event.getCode()) + " mode:" + toString(mMode));
}
void SDAWarpingVizApp::keyUp(KeyEvent event)
{
	if (!Warp::handleKeyUp(mWarps, event)) {
		if (!mSDASession->handleKeyUp(event)) {
		}
	}
}

void SDAWarpingVizApp::draw()
{
	gl::clear(Color::black());
	if (mFadeInDelay) {
		mSDASettings->iAlpha = 0.0f;
		if (getElapsedFrames() > mSDASession->getFadeInDelay()) {
			mFadeInDelay = false;
			timeline().apply(&mSDASettings->iAlpha, 0.0f, 1.0f, 1.5f, EaseInCubic());
		}
	}

	xLeft = 0;
	xRight = mSDASettings->mRenderWidth;
	yLeft = 0;
	yRight = mSDASettings->mRenderHeight;
	if (mFlipV) {
		yLeft = yRight;
		yRight = 0;
	}
	if (mFlipH) {
		xLeft = xRight;
		xRight = 0;
	}
	Rectf rectangle = Rectf(xLeft, yLeft, xRight, yRight);
	gl::setMatricesWindow(toPixels(getWindowSize()));

	if (mMode == SHARED) {
		mSpoutTexture = mSpoutIn.receiveTexture();
	}
	if (mSDASettings->mCursorVisible) {
		gl::ScopedBlendAlpha alpha;
		gl::enableAlphaBlending();
		// original
		gl::draw(mSpoutTexture, Rectf(0, 0, tWidth, tHeight));
		gl::drawString("Original", vec2(toPixels(0), toPixels(tHeight)), Color(1, 1, 1), Font("Verdana", toPixels(16)));
		// flipH
		gl::draw(mSpoutTexture, Rectf(tWidth * 2 + margin, 0, tWidth + margin, tHeight));
		gl::drawString("FlipH", vec2(toPixels(tWidth + margin), toPixels(tHeight)), Color(1, 1, 1), Font("Verdana", toPixels(16)));
		// flipV
		gl::draw(mSpoutTexture, Rectf(0, tHeight * 2 + margin, tWidth, tHeight + margin));
		gl::drawString("FlipV", vec2(toPixels(0), toPixels(tHeight * 2 + margin)), Color(1, 1, 1), Font("Verdana", toPixels(16)));
		if (mMode == SHADER) {
			// show the FBO color texture 
			gl::draw(mFbo->getColorTexture(), Rectf(tWidth + margin, tHeight + margin, tWidth * 2 + margin, tHeight * 2 + margin));
			gl::drawString("Shader", vec2(toPixels(tWidth + margin), toPixels(tHeight * 2 + margin)), Color(1, 1, 1), Font("Verdana", toPixels(16)));

		}
		if (mMode == SHARED) {
			if (mSpoutTexture) {
				gl::drawString("Receiving from: " + mSpoutIn.getSenderName(), vec2(toPixels(20), getWindowHeight() - toPixels(30)), Color(1, 1, 1), Font("Verdana", toPixels(24)));
				// Show the user what it is receiving
				gl::ScopedBlendAlpha alpha;
				gl::enableAlphaBlending();
				gl::drawString("fps: " + std::to_string((int)getAverageFps()), vec2(getWindowWidth() - toPixels(100), getWindowHeight() - toPixels(30)), Color(1, 1, 1), Font("Verdana", toPixels(24)));
				gl::drawString("RH click to select a sender", vec2(toPixels(20), getWindowHeight() - toPixels(60)), Color(1, 1, 1), Font("Verdana", toPixels(24)));

			}
		}
		else {
			gl::drawString("No sender/texture detected", vec2(toPixels(20), toPixels(20)), Color(1, 1, 1), Font("Verdana", toPixels(24)));

		}
		gl::drawString("yLeft: " + std::to_string(yLeft), vec2(getWindowWidth() - toPixels(100), getWindowHeight() - toPixels(30)), Color(1, 1, 1), Font("Verdana", toPixels(24)));

	}

	// iterate over the warps and draw their content
	for (auto &warp : mWarps) {
		// there are two ways you can use the warps:
		if (mUseBeginEnd) {
			// a) issue your draw commands between begin() and end() statements
			warp->begin();
			switch (mMode)
			{
			case SHADER:
				gl::draw(mFbo->getColorTexture(), rectangle);
				break;
			case IMAGE:
				if (mImage) {
					gl::draw(mImage, mSrcArea, warp->getBounds());
				}
				break;
			case SHARED:
				
				if (mSpoutTexture) {
					gl::draw(mSpoutTexture, rectangle);
				}
				break;
			default:
				break;
			}
			warp->end();
		}
		else {
			// b) simply draw a texture on them (ideal for video)
			// in this demo, we want to draw a specific area of our image,
			// but if you want to draw the whole image, you can simply use: warp->draw( mImage );
			switch (mMode)
			{
			case SHADER:
				warp->draw(mFbo->getColorTexture(), mSrcArea);
				break;
			case IMAGE:
				if (mImage) {
					warp->draw(mImage, mSrcArea);
				}
				break;
			case SHARED:
				
				if (mSpoutTexture) {
					warp->draw(mSpoutTexture, mSrcArea);
				}
				break;
			default:
				break;
			}
			warp->end();
		}
	}
	// NDI
	long long timecode = getElapsedFrames();
	XmlTree msg{ "ci_meta", mSDASettings->sFps + " fps SDAWarpingViz" };
	mNDISender.sendMetadata(msg, timecode);
	switch (mMode)
	{
	case SHADER:
		mSurface = Surface::create(mFbo->getColorTexture()->createSource());
		mNDISender.sendSurface(*mSurface, timecode);
		break;
	case IMAGE:

		break;
	case SHARED:
		
		if (mSpoutTexture) {
			mSurface = Surface::create(mSpoutTexture->createSource());
			mNDISender.sendSurface(*mSurface, timecode);
		}
		break;
	default:
		break;
	}

	getWindow()->setTitle(mSDASettings->sFps + " fps SDAViz");
}

void prepareSettings(App::Settings *settings)
{
	settings->setWindowSize(800, 600);
	settings->setConsoleWindowEnabled();
}

CINDER_APP(SDAWarpingVizApp, RendererGl, prepareSettings)

