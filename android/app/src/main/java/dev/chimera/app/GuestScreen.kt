package dev.chimera.app

import android.content.Context
import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.RippleDrawable
import android.view.Gravity
import android.view.View
import android.widget.FrameLayout
import android.widget.ImageButton
import android.widget.PopupMenu
import android.widget.TextView
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import kotlin.math.roundToInt

internal data class GuestViewport(val width: Int, val height: Int) {
    companion object {
        fun fromPixels(width: Int, height: Int): GuestViewport {
            require(width > 0 && height > 0)
            val scale = minOf(720.0 / minOf(width, height), 2048.0 / maxOf(width, height))
            return GuestViewport((width * scale).roundToInt().coerceIn(1, 2048),
                (height * scale).roundToInt().coerceIn(1, 2048))
        }
    }
}

internal class GuestScreen(context: Context, onLibrary: () -> Unit, onExport: () -> Unit) : FrameLayout(context) {
    val image = GuestImageView(context).apply { contentDescription = "客机画面" }
    private val status = TextView(context).apply {
        setTextColor(Color.WHITE)
        textSize = 15f
        gravity = Gravity.CENTER
        setPadding(dp(16), dp(8), dp(16), dp(8))
        background = GradientDrawable().apply { setColor(0xcc161616.toInt()); cornerRadius = dp(12).toFloat() }
        accessibilityLiveRegion = View.ACCESSIBILITY_LIVE_REGION_POLITE
    }

    init {
        setBackgroundColor(Color.BLACK)
        addView(image, LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT))
        addView(status, LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT, Gravity.CENTER))
        showStatus("正在打开应用…")
        val more = ImageButton(context).apply {
            contentDescription = "更多选项"
            setImageResource(R.drawable.ic_more_vertical)
            setPadding(dp(12), dp(12), dp(12), dp(12))
            val shape = GradientDrawable().apply { shape = GradientDrawable.OVAL; setColor(0xaa202124.toInt()) }
            background = RippleDrawable(ColorStateList.valueOf(0x55ffffff), shape, null)
            setOnClickListener { anchor ->
                val popup = PopupMenu(context, anchor, Gravity.RIGHT)
                popup.menu.add(0, 1, 0, "应用库").setOnMenuItemClickListener { onLibrary(); true }
                popup.menu.add(0, 2, 1, "导出日志").setOnMenuItemClickListener { onExport(); true }
                popup.show()
            }
        }
        val margin = dp(8)
        addView(more, LayoutParams(dp(48), dp(48), Gravity.TOP or Gravity.RIGHT).apply {
            topMargin = margin
            rightMargin = margin
        })
        ViewCompat.setOnApplyWindowInsetsListener(this) { _, insets ->
            val safe = insets.getInsets(WindowInsetsCompat.Type.displayCutout() or WindowInsetsCompat.Type.systemBars())
            val params = more.layoutParams as LayoutParams
            if (params.topMargin != margin + safe.top || params.rightMargin != margin + safe.right) {
                params.topMargin = margin + safe.top
                params.rightMargin = margin + safe.right
                more.layoutParams = params
            }
            insets
        }
    }

    fun showStatus(message: String?) {
        if (message == null && status.visibility == View.GONE) return
        status.text = message
        status.visibility = if (message == null) View.GONE else View.VISIBLE
    }

    private fun dp(value: Int) = (value * resources.displayMetrics.density).roundToInt()
}
