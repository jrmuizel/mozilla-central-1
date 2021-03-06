/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla Android code.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2009-2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Patrick Walton <pcwalton@mozilla.com>
 *   Chris Lord <chrislord.net@gmail.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

package org.mozilla.gecko.gfx;

import org.mozilla.gecko.FloatUtils;
import org.mozilla.gecko.GeckoApp;
import org.mozilla.gecko.GeckoAppShell;
import org.mozilla.gecko.GeckoEvent;
import org.mozilla.gecko.GeckoEventResponder;
import org.json.JSONException;
import org.json.JSONObject;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Point;
import android.graphics.PointF;
import android.graphics.Rect;
import android.graphics.RectF;
import android.os.SystemClock;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.View;

public class GeckoLayerClient implements GeckoEventResponder,
                                         FlexibleGLSurfaceView.Listener {
    private static final String LOGTAG = "GeckoLayerClient";

    private LayerController mLayerController;
    private LayerRenderer mLayerRenderer;
    private boolean mLayerRendererInitialized;

    private IntSize mScreenSize;
    private IntSize mWindowSize;
    private DisplayPortMetrics mDisplayPort;
    private DisplayPortMetrics mReturnDisplayPort;

    private VirtualLayer mRootLayer;

    /* The Gecko viewport as per the UI thread. Must be touched only on the UI thread. */
    private ViewportMetrics mGeckoViewport;

    /*
     * The viewport metrics being used to draw the current frame. This is only
     * accessed by the compositor thread, and so needs no synchronisation.
     */
    private ImmutableViewportMetrics mFrameMetrics;

    private String mLastCheckerboardColor;

    /* Used by robocop for testing purposes */
    private DrawListener mDrawListener;

    /* Used as a temporary ViewTransform by syncViewportInfo */
    private ViewTransform mCurrentViewTransform;

    public GeckoLayerClient(Context context) {
        // we can fill these in with dummy values because they are always written
        // to before being read
        mScreenSize = new IntSize(0, 0);
        mWindowSize = new IntSize(0, 0);
        mDisplayPort = new DisplayPortMetrics();
        mCurrentViewTransform = new ViewTransform(0, 0, 1);
    }

    /** Attaches the root layer to the layer controller so that Gecko appears. */
    void setLayerController(LayerController layerController) {
        LayerView view = layerController.getView();

        mLayerController = layerController;

        mRootLayer = new VirtualLayer(new IntSize(view.getWidth(), view.getHeight()));
        mLayerRenderer = new LayerRenderer(view);

        GeckoAppShell.registerGeckoEventListener("Viewport:Update", this);
        GeckoAppShell.registerGeckoEventListener("Viewport:PageSize", this);
        GeckoAppShell.registerGeckoEventListener("Viewport:CalculateDisplayPort", this);
        GeckoAppShell.registerGeckoEventListener("Checkerboard:Toggle", this);

        view.setListener(this);
        view.setLayerRenderer(mLayerRenderer);
        layerController.setRoot(mRootLayer);

        sendResizeEventIfNecessary(true);
    }

    DisplayPortMetrics getDisplayPort() {
        return mDisplayPort;
    }

    /* Informs Gecko that the screen size has changed. */
    private void sendResizeEventIfNecessary(boolean force) {
        DisplayMetrics metrics = new DisplayMetrics();
        GeckoApp.mAppContext.getWindowManager().getDefaultDisplay().getMetrics(metrics);
        View view = mLayerController.getView();

        IntSize newScreenSize = new IntSize(metrics.widthPixels, metrics.heightPixels);
        IntSize newWindowSize = new IntSize(view.getWidth(), view.getHeight());

        boolean screenSizeChanged = !mScreenSize.equals(newScreenSize);
        boolean windowSizeChanged = !mWindowSize.equals(newWindowSize);

        if (!force && !screenSizeChanged && !windowSizeChanged) {
            return;
        }

        mScreenSize = newScreenSize;
        mWindowSize = newWindowSize;

        if (screenSizeChanged) {
            Log.d(LOGTAG, "Screen-size changed to " + mScreenSize);
        }

        if (windowSizeChanged) {
            Log.d(LOGTAG, "Window-size changed to " + mWindowSize);
        }

        GeckoEvent event = GeckoEvent.createSizeChangedEvent(mWindowSize.width, mWindowSize.height,
                                                             mScreenSize.width, mScreenSize.height);
        GeckoAppShell.sendEventToGecko(event);
    }

    public Bitmap getBitmap() {
        return null;
    }

    void viewportSizeChanged() {
        // here we send gecko a resize message. The code in browser.js is responsible for
        // picking up on that resize event, modifying the viewport as necessary, and informing
        // us of the new viewport.
        sendResizeEventIfNecessary(true);
        // the following call also sends gecko a message, which will be processed after the resize
        // message above has updated the viewport. this message ensures that if we have just put
        // focus in a text field, we scroll the content so that the text field is in view.
        GeckoAppShell.viewSizeChanged();
    }

    private void adjustViewport() {
        ViewportMetrics viewportMetrics =
            new ViewportMetrics(mLayerController.getViewportMetrics());

        viewportMetrics.setViewport(viewportMetrics.getClampedViewport());

        mDisplayPort = DisplayPortCalculator.calculate(mLayerController.getViewportMetrics(),
                mLayerController.getPanZoomController().getVelocityVector());
        GeckoAppShell.sendEventToGecko(GeckoEvent.createViewportEvent(viewportMetrics, mDisplayPort));
        mGeckoViewport = viewportMetrics;
    }

    /**
     * The different types of Viewport messages handled. All viewport events
     * expect a display-port to be returned, but can handle one not being
     * returned.
     */
    private enum ViewportMessageType {
        UPDATE,       // The viewport has changed and should be entirely updated
        PAGE_SIZE     // The viewport's page-size has changed
    }

    /** Viewport message handler. */
    private void handleViewportMessage(JSONObject message, ViewportMessageType type) throws JSONException {
        ViewportMetrics messageMetrics = new ViewportMetrics(message);
        synchronized (mLayerController) {
            final ViewportMetrics newMetrics;
            ImmutableViewportMetrics oldMetrics = mLayerController.getViewportMetrics();

            switch (type) {
            default:
            case UPDATE:
                newMetrics = messageMetrics;
                // Keep the old viewport size
                newMetrics.setSize(oldMetrics.getSize());
                mLayerController.abortPanZoomAnimation();
                break;
            case PAGE_SIZE:
                newMetrics = new ViewportMetrics(oldMetrics);
                newMetrics.setPageSize(messageMetrics.getPageSize());
                break;
            }

            mLayerController.post(new Runnable() {
                public void run() {
                    mGeckoViewport = newMetrics;
                }
            });
            mLayerController.setViewportMetrics(newMetrics);
            mDisplayPort = DisplayPortCalculator.calculate(mLayerController.getViewportMetrics(), null);
        }
        mReturnDisplayPort = mDisplayPort;
    }

    /** Implementation of GeckoEventResponder/GeckoEventListener. */
    public void handleMessage(String event, JSONObject message) {
        try {
            if ("Viewport:Update".equals(event)) {
                handleViewportMessage(message, ViewportMessageType.UPDATE);
            } else if ("Viewport:PageSize".equals(event)) {
                handleViewportMessage(message, ViewportMessageType.PAGE_SIZE);
            } else if ("Viewport:CalculateDisplayPort".equals(event)) {
                ImmutableViewportMetrics newMetrics = new ImmutableViewportMetrics(new ViewportMetrics(message));
                mReturnDisplayPort = DisplayPortCalculator.calculate(newMetrics, null);
            } else if ("Checkerboard:Toggle".equals(event)) {
                try {
                    boolean showChecks = message.getBoolean("value");
                    mLayerController.setCheckerboardShowChecks(showChecks);
                    Log.i(LOGTAG, "Showing checks: " + showChecks);
                } catch(JSONException ex) {
                    Log.e(LOGTAG, "Error decoding JSON", ex);
                }
            }
        } catch (JSONException e) {
            Log.e(LOGTAG, "Unable to create viewport metrics in " + event + " handler", e);
        }
    }

    /** Implementation of GeckoEventResponder. */
    public String getResponse() {
        // We are responding to the events handled in handleMessage() above with the
        // display port we calculated. Different messages will generate different
        // display ports and put them in mReturnDisplayPort, so we just return that.
        // Note that mReturnDisplayPort is always touched on the Gecko thread, so
        // no synchronization is needed for it.
        if (mReturnDisplayPort == null) {
            return "";
        }
        try {
            return mReturnDisplayPort.toJSON();
        } finally {
            mReturnDisplayPort = null;
        }
    }

    void geometryChanged() {
        /* Let Gecko know if the screensize has changed */
        sendResizeEventIfNecessary(false);
        if (mLayerController.getRedrawHint())
            adjustViewport();
    }

    /*
     * This function returns the last viewport that we sent to Gecko. If any additional events are
     * being sent to Gecko that are relative on the Gecko viewport position, they must (a) be relative
     * to this viewport, and (b) be sent on the UI thread to avoid races. As long as these two
     * conditions are satisfied, and the events being sent to Gecko are processed in FIFO order, the
     * events will properly be relative to the Gecko viewport position. Note that if Gecko updates
     * its viewport independently, we get notified synchronously and also update this on the UI thread.
     */
    public ViewportMetrics getGeckoViewportMetrics() {
        return mGeckoViewport;
    }

    /** This function is invoked by Gecko via JNI; be careful when modifying signature.
      * The compositor invokes this function just before compositing a frame where the document
      * is different from the document composited on the last frame. In these cases, the viewport
      * information we have in Java is no longer valid and needs to be replaced with the new
      * viewport information provided. setPageSize will never be invoked on the same frame that
      * this function is invoked on; and this function will always be called prior to syncViewportInfo.
      */
    public void setFirstPaintViewport(float offsetX, float offsetY, float zoom, float pageWidth, float pageHeight) {
        synchronized (mLayerController) {
            ViewportMetrics currentMetrics = new ViewportMetrics(mLayerController.getViewportMetrics());
            currentMetrics.setOrigin(new PointF(offsetX, offsetY));
            currentMetrics.setZoomFactor(zoom);
            currentMetrics.setPageSize(new FloatSize(pageWidth, pageHeight));
            mLayerController.setViewportMetrics(currentMetrics);
            // At this point, we have just switched to displaying a different document than we
            // we previously displaying. This means we need to abort any panning/zooming animations
            // that are in progress and send an updated display port request to browser.js as soon
            // as possible. We accomplish this by passing true to abortPanZoomAnimation, which
            // sends the request after aborting the animation. The display port request is actually
            // a full viewport update, which is fine because if browser.js has somehow moved to
            // be out of sync with this first-paint viewport, then we force them back in sync.
            mLayerController.abortPanZoomAnimation();
            mLayerController.getView().setPaintState(LayerView.PAINT_BEFORE_FIRST);
        }
    }

    /** This function is invoked by Gecko via JNI; be careful when modifying signature.
      * The compositor invokes this function whenever it determines that the page size
      * has changed (based on the information it gets from layout). If setFirstPaintViewport
      * is invoked on a frame, then this function will not be. For any given frame, this
      * function will be invoked before syncViewportInfo.
      */
    public void setPageSize(float zoom, float pageWidth, float pageHeight) {
        synchronized (mLayerController) {
            // adjust the page dimensions to account for differences in zoom
            // between the rendered content (which is what the compositor tells us)
            // and our zoom level (which may have diverged).
            float ourZoom = mLayerController.getZoomFactor();
            pageWidth = pageWidth * ourZoom / zoom;
            pageHeight = pageHeight * ourZoom /zoom;
            mLayerController.setPageSize(new FloatSize(pageWidth, pageHeight));
            // Here the page size of the document has changed, but the document being displayed
            // is still the same. Therefore, we don't need to send anything to browser.js; any
            // changes we need to make to the display port will get sent the next time we call
            // adjustViewport().
        }
    }

    /** This function is invoked by Gecko via JNI; be careful when modifying signature.
      * The compositor invokes this function on every frame to figure out what part of the
      * page to display, and to inform Java of the current display port. Since it is called
      * on every frame, it needs to be ultra-fast.
      * It avoids taking any locks or allocating any objects. We keep around a
      * mCurrentViewTransform so we don't need to allocate a new ViewTransform
      * everytime we're called. NOTE: we might be able to return a ImmutableViewportMetrics
      * which would avoid the copy into mCurrentViewTransform.
      */
    public ViewTransform syncViewportInfo(int x, int y, int width, int height, float resolution, boolean layersUpdated) {
        // getViewportMetrics is thread safe so we don't need to synchronize
        // on mLayerController.
        // We save the viewport metrics here, so we later use it later in
        // createFrame (which will be called by nsWindow::DrawWindowUnderlay on
        // the native side, by the compositor). The LayerController's viewport
        // metrics can change between here and there, as it's accessed outside
        // of the compositor thread.
        mFrameMetrics = mLayerController.getViewportMetrics();

        mCurrentViewTransform.x = mFrameMetrics.viewportRectLeft;
        mCurrentViewTransform.y = mFrameMetrics.viewportRectTop;
        mCurrentViewTransform.scale = mFrameMetrics.zoomFactor;

        mRootLayer.setPositionAndResolution(x, y, x + width, y + height, resolution);

        if (layersUpdated && mDrawListener != null) {
            /* Used by robocop for testing purposes */
            mDrawListener.drawFinished();
        }

        return mCurrentViewTransform;
    }

    /** This function is invoked by Gecko via JNI; be careful when modifying signature. */
    public LayerRenderer.Frame createFrame() {
        // Create the shaders and textures if necessary.
        if (!mLayerRendererInitialized) {
            mLayerRenderer.checkMonitoringEnabled();
            mLayerRenderer.createDefaultProgram();
            mLayerRendererInitialized = true;
        }

        // Build the contexts and create the frame.
        Layer.RenderContext pageContext = mLayerRenderer.createPageContext(mFrameMetrics);
        Layer.RenderContext screenContext = mLayerRenderer.createScreenContext();
        return mLayerRenderer.createFrame(pageContext, screenContext);
    }

    /** This function is invoked by Gecko via JNI; be careful when modifying signature. */
    public void activateProgram() {
        mLayerRenderer.activateDefaultProgram();
    }

    /** This function is invoked by Gecko via JNI; be careful when modifying signature. */
    public void deactivateProgram() {
        mLayerRenderer.deactivateDefaultProgram();
    }

    /** Implementation of FlexibleGLSurfaceView.Listener */
    public void renderRequested() {
        GeckoAppShell.scheduleComposite();
    }

    /** Implementation of FlexibleGLSurfaceView.Listener */
    public void compositionPauseRequested() {
        GeckoAppShell.schedulePauseComposition();
    }

    /** Implementation of FlexibleGLSurfaceView.Listener */
    public void compositionResumeRequested() {
        GeckoAppShell.scheduleResumeComposition();
    }

    /** Implementation of FlexibleGLSurfaceView.Listener */
    public void surfaceChanged(int width, int height) {
        compositionPauseRequested();
        mLayerController.setViewportSize(new FloatSize(width, height));
        compositionResumeRequested();
        renderRequested();
    }

    /** Used by robocop for testing purposes. Not for production use! This is called via reflection by robocop. */
    public void setDrawListener(DrawListener listener) {
        mDrawListener = listener;
    }

    /** Used by robocop for testing purposes. Not for production use! This is used via reflection by robocop. */
    public interface DrawListener {
        public void drawFinished();
    }
}

