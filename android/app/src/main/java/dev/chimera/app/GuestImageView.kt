package dev.chimera.app

import android.content.Context
import android.graphics.Matrix
import android.graphics.PointF
import android.graphics.drawable.BitmapDrawable
import android.view.MotionEvent
import android.view.ViewConfiguration
import androidx.appcompat.widget.AppCompatImageView
import kotlin.math.hypot

class GuestImageView(context: Context) : AppCompatImageView(context) {
    var onGuestTap: ((PointF, PointF) -> Unit)? = null
    var acceptInput = false
        set(value) {
            field = value
            if (!value) cancelGesture()
        }
    private var start: PointF? = null
    private var startX = 0f
    private var startY = 0f
    private var pointer = -1
    private val slop = ViewConfiguration.get(context).scaledTouchSlop

    init {
        isClickable = true
        scaleType = ScaleType.FIT_CENTER
    }

    private fun guestPoint(x: Float, y: Float): PointF? {
        val image = drawable as? BitmapDrawable ?: return null
        if (image.intrinsicWidth <= 0 || image.intrinsicHeight <= 0) return null
        val inverse = Matrix()
        if (!imageMatrix.invert(inverse)) return null
        val point = floatArrayOf(x - paddingLeft, y - paddingTop)
        inverse.mapPoints(point)
        if (!point.all { it.isFinite() } || point[0] < 0 || point[1] < 0 ||
            point[0] >= image.intrinsicWidth || point[1] >= image.intrinsicHeight) return null
        return PointF(point[0] * image.bitmap.width / image.intrinsicWidth,
            point[1] * image.bitmap.height / image.intrinsicHeight)
    }

    private fun cancelGesture() {
        start = null
        pointer = -1
        parent?.requestDisallowInterceptTouchEvent(false)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (!acceptInput) { cancelGesture(); return false }
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                cancelGesture()
                val point = guestPoint(event.x, event.y) ?: return false
                start = point
                startX = event.x
                startY = event.y
                pointer = event.getPointerId(0)
                parent?.requestDisallowInterceptTouchEvent(true)
                return true
            }
            MotionEvent.ACTION_MOVE -> {
                val index = event.findPointerIndex(pointer)
                if (index < 0 || hypot(event.getX(index) - startX, event.getY(index) - startY) > slop)
                    cancelGesture()
            }
            MotionEvent.ACTION_POINTER_DOWN, MotionEvent.ACTION_CANCEL -> cancelGesture()
            MotionEvent.ACTION_UP -> {
                val from = start
                val index = event.findPointerIndex(pointer)
                val to = if (index >= 0 && hypot(event.getX(index) - startX, event.getY(index) - startY) <= slop)
                    guestPoint(event.getX(index), event.getY(index)) else null
                cancelGesture()
                if (from != null && to != null) {
                    performClick()
                    onGuestTap?.invoke(from, to)
                }
            }
        }
        return true
    }

    override fun performClick(): Boolean {
        super.performClick()
        return true
    }

    override fun onDetachedFromWindow() {
        cancelGesture()
        super.onDetachedFromWindow()
    }
}