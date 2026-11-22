package dev.chimera.app

import android.content.Context
import android.graphics.Canvas
import android.graphics.PointF
import android.util.Log
import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import kotlin.math.hypot
import kotlin.math.min

class GuestImageView(context: Context) : View(context) {
    var onGuestTap: ((PointF, PointF) -> Unit)? = null
    var onGuestScroll: ((PointF, Float) -> Unit)? = null
    var onBackend: ((Boolean) -> Unit)? = null
    var acceptInput = false
        set(value) {
            field = value
            if (!value) cancelGesture()
        }
    private var frame: GuestFrame? = null
    val hasFrame: Boolean get() = frame != null
    private var backend: Boolean? = null
    private var start: PointF? = null
    private var startX = 0f
    private var startY = 0f
    private var pointer = -1
    private var dragged = false
    private val slop = ViewConfiguration.get(context).scaledTouchSlop

    init {
        isClickable = true
        setWillNotDraw(false)
    }

    fun submitFrame(value: GuestFrame?) {
        cancelGesture()
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
        start = null
        pointer = -1
        dragged = false
        parent?.requestDisallowInterceptTouchEvent(false)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (!acceptInput) { cancelGesture(); return false }
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                cancelGesture()
                start = guestPoint(event.x, event.y) ?: return false
                startX = event.x
                startY = event.y
                pointer = event.getPointerId(0)
                parent?.requestDisallowInterceptTouchEvent(true)
                return true
            }
            MotionEvent.ACTION_MOVE -> {
                val index = event.findPointerIndex(pointer)
                if (index < 0) cancelGesture()
                else if (hypot(event.getX(index) - startX, event.getY(index) - startY) > slop) dragged = true
            }
            MotionEvent.ACTION_POINTER_DOWN, MotionEvent.ACTION_CANCEL -> cancelGesture()
            MotionEvent.ACTION_UP -> {
                val from = start
                val index = event.findPointerIndex(pointer)
                val scroll = dragged || (index >= 0 && hypot(event.getX(index) - startX, event.getY(index) - startY) > slop)
                val to = if (index >= 0) guestPoint(event.getX(index), event.getY(index), !scroll) else null
                cancelGesture()
                if (from != null && to != null) {
                    if (scroll) onGuestScroll?.invoke(from, from.y - to.y)
                    else {
                        performClick()
                        onGuestTap?.invoke(from, to)
                    }
                }
            }
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