package net.perfectdreams.harmony.gl.shaders

import android.opengl.GLES20
import android.opengl.GLES30

class ShaderManager {
    /**
     * Loads the vertex shader and fragment shader by their file name from the application's resources
     */
    fun <T : GameShader> loadShader(vertexShaderCode: String, fragmentShaderCode: String, createGameShader: (Int) -> (T)): T {
        val programId = loadShader(vertexShaderCode, fragmentShaderCode)
        return createGameShader.invoke(programId)
    }

    /**
     * Loads the vertex shader and fragment shader
     */
    fun loadShader(vertexShaderCode: String, fragmentShaderCode: String): Int {
        val vertexShaderId = GLES20.glCreateShader(GLES20.GL_VERTEX_SHADER)
        val fragmentShaderId = GLES20.glCreateShader(GLES20.GL_FRAGMENT_SHADER)

        // Compile Vertex Shader
        checkAndCompile(vertexShaderId, vertexShaderCode)

        // Compile Fragment Shader
        checkAndCompile(fragmentShaderId, fragmentShaderCode)

        val programId = GLES20.glCreateProgram()
        GLES20.glAttachShader(programId, vertexShaderId)
        GLES20.glAttachShader(programId, fragmentShaderId)
        GLES20.glLinkProgram(programId)

        // Check Shader
        val status = IntArray(1)
        GLES30.glGetProgramiv(programId, GLES30.GL_LINK_STATUS, status, 0)

        // YES DON'T FORGET THAT WE NEED TO USE GL_TRUE!!
        // I was checking using == 0 and of course that doesn't work because that means FALSE (i think)
        if (status[0] != GLES20.GL_TRUE) {
            val log = GLES30.glGetShaderInfoLog(programId)
            GLES30.glDeleteShader(programId)
            error("Something went wrong while compiling shader $programId! Status: ${status[0]}; Info: $log")
        }

        GLES20.glDetachShader(programId, vertexShaderId)
        GLES20.glDetachShader(programId, fragmentShaderId)

        GLES20.glDeleteShader(vertexShaderId)
        GLES20.glDeleteShader(fragmentShaderId)

        return programId
    }

    private fun checkAndCompile(shaderId: Int, code: String) {
        // Compile Shader
        GLES20.glShaderSource(shaderId, code)
        GLES20.glCompileShader(shaderId)

        // Check Shader
        val status = IntArray(1)
        GLES30.glGetShaderiv(shaderId, GLES30.GL_COMPILE_STATUS, status, 0)

        // YES DON'T FORGET THAT WE NEED TO USE GL_TRUE!!
        // I was checking using == 0 and of course that doesn't work because that means FALSE (i think)
        if (status[0] != GLES20.GL_TRUE) {
            val log = GLES30.glGetShaderInfoLog(shaderId)
            GLES30.glDeleteShader(shaderId)
            error("Something went wrong while compiling shader $shaderId! Status: ${status[0]}; Info: $log")
        }
    }
}