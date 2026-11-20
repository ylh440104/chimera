package dev.chimera.app

class Native {
    companion object {
        init {
            System.loadLibrary("chimera")
        }

        @JvmStatic
        external fun run(path: String): Int
    }
}