package net.perfectdreams.harmony.gl.shaders

import android.opengl.GLES20
import android.opengl.GLES31
import java.nio.FloatBuffer

abstract class GameShader(val programId: Int) {
    private fun getUniformLocation(name: String): Int {
        val location = GLES20.glGetUniformLocation(programId, name)
        if (location == -1) {
            println("Program $programId does not have uniform \"$name\"!")
            return -1
        }

        return location
    }

    fun uniform1f(name: String): Uniform1f {
        return Uniform1f(this, getUniformLocation(name), name)
    }

    fun uniform1Bool(name: String): Uniform1Bool {
        return Uniform1Bool(this, getUniformLocation(name), name)
    }

    fun uniform2f(name: String): Uniform2f {
        return Uniform2f(this, getUniformLocation(name), name)
    }

    fun uniform3f(name: String): Uniform3f {
        return Uniform3f(this, getUniformLocation(name), name)
    }

    fun uniform4f(name: String): Uniform4f {
        return Uniform4f(this, getUniformLocation(name), name)
    }

    fun uniformMatrix4fv(name: String): UniformMatrix4fv {
        return UniformMatrix4fv(this, getUniformLocation(name), name)
    }

    fun uniform1i(name: String): Uniform1i {
        return Uniform1i(this, getUniformLocation(name), name)
    }

    fun uniform1Sampler2D(name: String): Uniform1Sampler2D {
        return Uniform1Sampler2D(this, getUniformLocation(name), name)
    }

    abstract class Uniform(val shader: GameShader, val location: Int, val name: String) {
        fun isUniformLocationInvalidAndLog(): Boolean {
            if (this.location == -1) {
                println("Program ${shader.programId} does not have uniform \"$name\"! Ignoring set...")
                return true
            }

            return false
        }
    }

    class Uniform1f(shader: GameShader, location: Int, name: String) : Uniform(shader, location, name) {
        fun set(x: Float) {
            if (isUniformLocationInvalidAndLog())
                return

            GLES31.glProgramUniform1f(shader.programId, location, x)
        }
    }

    class Uniform2f(shader: GameShader, location: Int, name: String) : Uniform(shader, location, name) {
        fun set(x: Float, y: Float) {
            if (isUniformLocationInvalidAndLog())
                return

            GLES31.glProgramUniform2f(shader.programId, location, x, y)
        }
    }

    class Uniform3f(shader: GameShader, location: Int, name: String) : Uniform(shader, location, name) {
        fun set(x: Float, y: Float, z: Float) {
            if (isUniformLocationInvalidAndLog())
                return

            GLES31.glProgramUniform3f(shader.programId, location, x, y, z)
        }
    }

    class Uniform4f(shader: GameShader, location: Int, name: String) : Uniform(shader, location, name) {
        fun set(x: Float, y: Float, z: Float, w: Float) {
            if (isUniformLocationInvalidAndLog())
                return

            GLES31.glProgramUniform4f(shader.programId, location, x, y, z, w)
        }
    }

    class UniformMatrix4fv(shader: GameShader, location: Int, name: String) : Uniform(shader, location, name) {
        fun set(value: FloatArray) {
            if (isUniformLocationInvalidAndLog())
                return

            // TODO: Is this correct?
            GLES31.glProgramUniformMatrix4fv(shader.programId, location, value.size, false, FloatBuffer.wrap(value))
        }
    }

    class Uniform1i(shader: GameShader, location: Int, name: String) : Uniform(shader, location, name) {
        fun set(x: Int) {
            if (isUniformLocationInvalidAndLog())
                return

            GLES31.glProgramUniform1i(shader.programId, location, x)
        }
    }

    class Uniform1Sampler2D(shader: GameShader, location: Int, name: String) : Uniform(shader, location, name) {
        fun set(slot: Int, textureId: Int) {
            if (isUniformLocationInvalidAndLog())
                return

            // Hacky!!!
            val pos = slot - 0x84C0

            GLES20.glActiveTexture(slot)
            GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, textureId)

            GLES31.glProgramUniform1i(shader.programId, location, pos)
        }
    }

    class Uniform1Bool(shader: GameShader, location: Int, name: String) : Uniform(shader, location, name) {
        fun set(x: Boolean) {
            if (isUniformLocationInvalidAndLog())
                return

            GLES31.glProgramUniform1i(shader.programId, location, if (x) 1 else 0)
        }
    }
}

fun <T : GameShader> T.bind(block: T.() -> (Unit)) {
    GLES31.glUseProgram(this.programId)

    block.invoke(this)
}