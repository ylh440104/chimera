package dev.chimera.app

import android.content.Context
import android.graphics.Canvas
import android.graphics.PointF
import android.util.Log
import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import kotlin.math.min

class GuestImageView(context: Context) : View(context) {
    var onGuestTap: ((PointF, PointF) -> Unit)? = null
    internal var onGuestPan: ((PanPhase, Float, Float) -> Unit)? = null
    var onBackend: ((Boolean) -> Unit)? = null
    var acceptInput = false
        set(value) {
            field = value
            if (!value) cancelGesture()
        }
    private var frame: GuestFrame? = null
    val hasFrame: Boolean get() = frame != null
    private var backend: Boolean? = null
    private var pointer = -1
    private val gesture = GuestGesture(
        { phase, x, y -> onGuestPan?.invoke(phase, x, y) },
        { x, y, upX, upY ->
            performClick()
            onGuestTap?.invoke(PointF(x, y), PointF(upX, upY))
        }
    )
    private val slop = ViewConfiguration.get(context).scaledTouchSlop

    init {
        isClickable = true
        setWillNotDraw(false)
    }

    fun submitFrame(value: GuestFrame?) {
        if (value == null || frame?.width != value.width || frame?.height != value.height) cancelGesture()
        frame = value
        invalidate()
    }

    private fun viewport(): FloatArray? {
        val f = frame ?: return null
        val w = width - paddingLeft - paddingRight
        val h = height - paddingTop - paddingBottom
        if (w <= 0 || h <= 0) return null
        val scale = min(w.toFloat() / f.width, h.toFloat() / f.height)
        return floatArrayOf(paddingLeft + (w - f.width * scale) / 2,
            paddingTop + (h - f.height * scale) / 2, scale)
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val f = frame ?: return
        val v = viewport() ?: return
        val accelerated = canvas.isHardwareAccelerated
        if (backend != accelerated) {
            backend = accelerated
            Log.i("chimera", "render: hardwareCanvas=$accelerated bitmapRaster=false")
            post { if (isAttachedToWindow) onBackend?.invoke(accelerated) }
        }
        val saved = canvas.save()
        try {
            canvas.translate(v[0], v[1])
            canvas.scale(v[2], v[2])
            canvas.clipRect(0f, 0f, f.width.toFloat(), f.height.toFloat())
            f.replay(canvas)
        } finally {
            canvas.restoreToCount(saved)
        }
    }

    private fun guestPoint(x: Float, y: Float, bounded: Boolean = true): PointF? {
        val f = frame ?: return null
        val v = viewport() ?: return null
        val gx = (x - v[0]) / v[2]
        val gy = (y - v[1]) / v[2]
        if (!gx.isFinite() || !gy.isFinite()) return null
        if (bounded && (gx < 0 || gy < 0 || gx >= f.width || gy >= f.height)) return null
        return PointF(gx, gy)
    }

    private fun cancelGesture() {
        gesture.cancel()
        pointer = -1
        parent?.requestDisallowInterceptTouchEvent(false)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (!acceptInput) { cancelGesture(); return false }
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                cancelGesture()
                val point = guestPoint(event.x, event.y) ?: return false
                val scale = viewport()?.get(2) ?: return false
                pointer = event.getPointerId(0)
                gesture.begin(point.x, point.y, slop / scale)
                parent?.requestDisallowInterceptTouchEvent(true)
            }
            MotionEvent.ACTION_MOVE, MotionEvent.ACTION_UP -> {
                val i = event.findPointerIndex(pointer)
                val point = if (i >= 0) guestPoint(event.getX(i), event.getY(i), false) else null
                val f = frame
                if (point == null || f == null) cancelGesture()
                else {
                    val up = event.actionMasked == MotionEvent.ACTION_UP
                    val inside = point.x >= 0 && point.y >= 0 && point.x < f.width && point.y < f.height
                    gesture.sample(point.x, point.y, up, inside)
                    if (up) cancelGesture()
                }
            }
            MotionEvent.ACTION_POINTER_DOWN, MotionEvent.ACTION_CANCEL -> cancelGesture()
        }
        return true
    }

    override fun performClick(): Boolean {
        super.performClick()
        return true
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        cancelGesture()
        super.onSizeChanged(w, h, oldw, oldh)
    }

    override fun onDetachedFromWindow() {
        cancelGesture()
        super.onDetachedFromWindow()
    }
}