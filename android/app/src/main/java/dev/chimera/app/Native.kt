package dev.chimera.app

class Native {
    companion object {
        init {
            System.loadLibrary("chimera")
        }

        @JvmStatic
        external fun load(path: String): String

        @JvmStatic
        external fun run(): String

        @JvmStatic
        external fun report(): String

        @JvmStatic
        external fun pump(): Int

        @JvmStatic
        external fun release()
    }
}