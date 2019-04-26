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

private:
	// Settings
	SDASettingsRef					mSDASettings;
	// Session
	SDASessionRef					mSDASession;
	// Log
	SDALogRef						mSDALog;

	// Spout
	SpoutIn							mSpoutIn;
	gl::Texture2dRef				mSpoutTexture;
	// fbo
	bool							mIsShutDown;
	Anim<float>						mRenderWindowTimer;
	void							positionRenderWindow();
	bool							mFadeInDelay;
	int								xLeft, xRight, yLeft, yRight;
	int								margin, tWidth, tHeight;
	//! fbos
	//void							renderToFbo();
	//gl::FboRef						mFbo;
	//! shaders
	//gl::GlslProgRef					mGlsl;
	// ndi
	//CinderNDISender					mNDISender;
	//ci::SurfaceRef 					mSurface;
	// warping
	fs::path		mSettings;
	gl::TextureRef	mImage;
	WarpList		mWarps;
	Area			mSrcArea;
};


SDAWarpingVizApp::SDAWarpingVizApp()
	//: mNDISender("WarpingViz")
{
	// Settings
	mSDASettings = SDASettings::create("WarpingViz");
	// Session
	mSDASession = SDASession::create(mSDASettings);
	mSDASession->getWindowsResolution();

	// warping
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
	//mFbo = gl::Fbo::create(mSDASettings->mRenderWidth, mSDASettings->mRenderHeight, format.depthTexture());
	// adjust the content size of the warps
	Warp::setSize(mWarps, vec2(mSDASettings->mRenderWidth, mSDASettings->mRenderHeight));
	mSDASession->setFloatUniformValueByIndex(mSDASettings->IRESX, mSDASettings->mRenderWidth);
	mSDASession->setFloatUniformValueByIndex(mSDASettings->IRESY, mSDASettings->mRenderHeight);
	mSrcArea = Area(0, 0, mSDASettings->mRenderWidth, mSDASettings->mRenderHeight);
	// ndi
	//mSurface = ci::Surface::create(mSDASettings->mRenderWidth, mSDASettings->mRenderHeight, true, SurfaceChannelOrder::BGRA);

	// shader
	//mGlsl = gl::GlslProg::create(gl::GlslProg::Format().vertex(loadAsset("passthrough.vs")).fragment(loadAsset("post.glsl")));

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
/*
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
	//if (mSpoutTexture) {
	//	mSpoutTexture->bind(0);
	//}
	//else {
	//	mImage->bind(0);
	//}
	mSDASession->getMixetteTexture()->bind(0);
	//mImage->bind(0);
	gl::ScopedGlslProg prog(mGlsl);

	mGlsl->uniform("iTime", (float)getElapsedSeconds());
	mGlsl->uniform("iResolution", vec3(mSDASettings->mRenderWidth, mSDASettings->mRenderHeight, 1.0));
	mGlsl->uniform("iChannel0", 0); // texture 0
	mGlsl->uniform("iExposure", 1.0f);
	mGlsl->uniform("iSobel", 1.0f);
	mGlsl->uniform("iChromatic", 1.0f);

	//gl::drawSolidRect(getWindowBounds());
	gl::drawSolidRect(Rectf(0, 0, mSDASettings->mRenderWidth, mSDASettings->mRenderHeight));
} */
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
		/*if (mSDASession->getMode() == mSDASettings->MODE_SHADER) {
			renderToFbo();
		}*/
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
	bool isAltDown = event.isAltDown();
	CI_LOG_V("main keydown: " + toString(event.getCode()) + " ctrl: " + toString(isModDown) + " shift: " + toString(isShiftDown) + " alt: " + toString(isAltDown));
	 
	// pass this key event to the warp editor first
	if (!Warp::handleKeyDown(mWarps, event)) {
		if (!mSDASession->handleKeyDown(event)) {
			switch (event.getCode()) {
			case KeyEvent::KEY_KP_PLUS:
			case KeyEvent::KEY_TAB:
			case KeyEvent::KEY_f:
				positionRenderWindow();
				break;


			case KeyEvent::KEY_w:
				CI_LOG_V("warp edit mode");
				if (!isModDown) {
					// toggle warp edit mode
					Warp::enableEditMode(!Warp::isEditModeEnabled());
				}
				break;
			/*case KeyEvent::KEY_c:
				if (isAltDown) {
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
				}
				break; */
			case KeyEvent::KEY_c:
				// mouse cursor and ui visibility
				mSDASettings->mCursorVisible = !mSDASettings->mCursorVisible;
				setUIVisibility(mSDASettings->mCursorVisible);
				break;
			}
		}
	}
	
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

	xLeft = 0;// mSDASession->getFloatUniformValueByName("iResolutionX") / 3.0f;
	xRight = mSDASettings->mRenderWidth;
	yLeft = 0;
	yRight = mSDASettings->mRenderHeight;
	if (mSDASettings->mFlipV) {
		yLeft = yRight;
		yRight = 0;
	}
	if (mSDASettings->mFlipH) {
		xLeft = xRight;
		xRight = 0;
	}
	mSrcArea = Area(xLeft, yLeft, xRight, yRight );
	 
	//Rectf rectangle = Rectf(xLeft, yLeft, xRight, yRight);
	gl::setMatricesWindow(toPixels(getWindowSize()));

	if (mSDASession->getMode() == mSDASettings->MODE_SHARED) {
		mSpoutTexture = mSpoutIn.receiveTexture();
	}
	if (mSDASettings->mCursorVisible) {
		gl::ScopedBlendAlpha alpha;
		gl::enableAlphaBlending();
		// original
		gl::draw(mSDASession->getMixetteTexture(), Rectf(0, 0, tWidth, tHeight));
		gl::drawString("Mixette", vec2(toPixels(xLeft), toPixels(tHeight)), Color(1, 1, 1), Font("Verdana", toPixels(16)));
		// flipH
		gl::draw(mSDASession->getRenderTexture(), Rectf(tWidth * 2 + margin, 0, tWidth + margin, tHeight));
		gl::drawString("Render", vec2(toPixels(xLeft + tWidth + margin), toPixels(tHeight)), Color(1, 1, 1), Font("Verdana", toPixels(16)));
		// flipV
		gl::draw(mSpoutTexture, Rectf(0, tHeight * 2 + margin, tWidth, tHeight + margin));
		gl::drawString("Spout", vec2(toPixels(xLeft), toPixels(tHeight * 2 + margin)), Color(1, 1, 1), Font("Verdana", toPixels(16)));
		
		// show the FBO color texture 
		gl::draw(mSDASession->getHydraTexture(), Rectf(tWidth + margin, tHeight + margin, tWidth * 2 + margin, tHeight * 2 + margin));
		gl::drawString("Hydra", vec2(toPixels(xLeft + tWidth + margin), toPixels(tHeight * 2 + margin)), Color(1, 1, 1), Font("Verdana", toPixels(16)));

		if (mSDASession->getMode() == mSDASettings->MODE_SHARED) {
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
		gl::drawString("irx: " + std::to_string(mSDASession->getFloatUniformValueByName("iResolutionX"))
			+ " iry: " + std::to_string(mSDASession->getFloatUniformValueByName("iResolutionY"))
			+ " fw: " + std::to_string(mSDASettings->mFboWidth)
			+ " fh: " + std::to_string(mSDASettings->mFboHeight),
			vec2(xLeft, getWindowHeight() - toPixels(30)), Color(1, 1, 1),
			Font("Verdana", toPixels(24)));
	}

	// iterate over the warps and draw their content
	for (auto &warp : mWarps) {	
		//warp->draw(mFbo->getColorTexture(), mSrcArea);
		
		if (mSDASession->getMode() == 0) warp->draw(mSDASession->getMixetteTexture(), mSrcArea);
		if (mSDASession->getMode() == 1) warp->draw(mSDASession->getMixTexture(), mSrcArea);
		if (mSDASession->getMode() == 2) warp->draw(mSDASession->getRenderTexture(), mSrcArea);
		if (mSDASession->getMode() == 3) warp->draw(mSDASession->getHydraTexture(), mSrcArea);
		if (mSDASession->getMode() == 4) warp->draw(mSDASession->getFboTexture(0), mSrcArea);
		if (mSDASession->getMode() == 5) warp->draw(mSDASession->getFboTexture(1), mSrcArea);
		if (mSDASession->getMode() == 6) warp->draw(mSDASession->getFboTexture(2), mSrcArea);
		if (mSDASession->getMode() == 7) warp->draw(mSDASession->getFboTexture(3), mSrcArea);
		if (mSDASession->getMode() == 8) warp->draw(mSDASession->getFboTexture(4), mSrcArea);

		/*switch (mSDASession->getMode())
		{
		case mSDASettings->MODE_SHADER:
			warp->draw(mFbo->getColorTexture(), mSrcArea);
			break;
		case mSDASettings->MODE_IMAGE:
			if (mImage) {
				warp->draw(mImage, mSrcArea);
			}
			break;
		case mSDASettings->MODE_SHARED:
			if (mSpoutTexture) {
				warp->draw(mSpoutTexture, mSrcArea);
			}
			break;
		case mSDASettings->MODE_MIX:
			warp->draw(mSDASession->getMixTexture(), mSrcArea);
			break;
		default:
			break;
		} */
	}
	// NDI
	/*long long timecode = getElapsedFrames();
	XmlTree msg{ "ci_meta", mSDASettings->sFps + " fps SDAWarpingViz" };
	mNDISender.sendMetadata(msg, timecode);
	switch (mSDASession->getMode())
	{
	case mSDASettings->MODE_SHADER:
		mSurface = Surface::create(mFbo->getColorTexture()->createSource());
		mNDISender.sendSurface(*mSurface, timecode);
		break;
	case mSDASettings->MODE_IMAGE:

		break;
	case mSDASettings->MODE_SHARED:
		
		if (mSpoutTexture) {
			mSurface = Surface::create(mSpoutTexture->createSource());
			mNDISender.sendSurface(*mSurface, timecode);
		}
		break;
	default:
		break;
	} */

	getWindow()->setTitle(mSDASettings->sFps + " fps WarpViz");
}

void prepareSettings(App::Settings *settings)
{
	settings->setWindowSize(1280, 720);
	settings->setConsoleWindowEnabled();
}

CINDER_APP(SDAWarpingVizApp, RendererGl, prepareSettings)

