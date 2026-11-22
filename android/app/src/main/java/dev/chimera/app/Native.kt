package dev.chimera.app

class Native {
    companion object {
        val executor = java.util.concurrent.Executors.newSingleThreadExecutor()
        init {
            System.loadLibrary("chimera")
        }

        @JvmStatic
        external fun load(path: String, sandbox: String): String

        @JvmStatic
        external fun run(): String

        @JvmStatic
        external fun report(): String

        @JvmStatic
        external fun pump(): Int

        @JvmStatic
        external fun ready(): Boolean

        @JvmStatic
        external fun render(width: Int, height: Int): IntArray?

        @JvmStatic
        external fun paint(target: FramePainter, width: Int, height: Int): Boolean

        @JvmStatic
        external fun tap(downX: Double, downY: Double, upX: Double, upY: Double): Int

        @JvmStatic
        external fun scroll(x: Double, y: Double, delta: Double): Int

        @JvmStatic
        external fun release()
    }
}