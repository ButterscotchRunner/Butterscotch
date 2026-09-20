package net.perfectdreams.butterscotch.android.shaders

import net.perfectdreams.harmony.gl.shaders.GameShader

class BlitShader(programId: Int) : GameShader(programId) {
    val uTexture = uniform1Sampler2D("uTexture")
}
