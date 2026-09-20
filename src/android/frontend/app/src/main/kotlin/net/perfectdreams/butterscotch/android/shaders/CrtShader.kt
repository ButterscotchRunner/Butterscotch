package net.perfectdreams.butterscotch.android.shaders

import net.perfectdreams.harmony.gl.shaders.GameShader

class CrtShader(programId: Int) : GameShader(programId) {
    val uTexture = uniform1Sampler2D("uTexture")
    val uResolution = uniform2f("uResolution")
    val uCurvature = uniform1f("uCurvature")
    val uAberration = uniform1f("uAberration")
    val uHalation = uniform1f("uHalation")
    val uScanlines = uniform1f("uScanlines")
    val uMask = uniform1f("uMask")
    val uVignette = uniform1f("uVignette")
}
