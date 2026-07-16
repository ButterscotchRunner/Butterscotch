#ifndef _BS_MATRIX_MATH_H_
#define _BS_MATRIX_MATH_H_
#include "common.h"
#include "math_compat.h"
#include <string.h>

// ===[ Matrix4f Type ]===

// Column-major 4x4 matrix (OpenGL native layout)
// Layout:
//   m[0]  m[4]  m[8]   m[12] (tx)
//   m[1]  m[5]  m[9]   m[13] (ty)
//   m[2]  m[6]  m[10]  m[14] (tz)
//   m[3]  m[7]  m[11]  m[15] (1)
typedef struct {
    float m[16]; // m[col*4 + row]
} Matrix4f;

#define Matrix_getIndex(row,col) ((col)*4 + (row))

// ===[ Identity / Copy ]===

static inline Matrix4f* Matrix4f_identity(Matrix4f* dest) {
    memset(dest->m, 0, sizeof(dest->m));
    dest->m[0] = 1.0f;
    dest->m[5] = 1.0f;
    dest->m[10] = 1.0f;
    dest->m[15] = 1.0f;
    return dest;
}

static inline Matrix4f* Matrix4f_copy(Matrix4f* dest, const Matrix4f* src) {
    memcpy(dest->m, src->m, sizeof(dest->m));
    return dest;
}

// ===[ Multiply ]===

// dest = a * b (safe if dest aliases a or b)
static inline Matrix4f* Matrix4f_multiply(Matrix4f* dest, const Matrix4f* a, const Matrix4f* b) {
    float tmp[16];
    for (int col = 0; 4 > col; col++) {
        for (int row = 0; 4 > row; row++) {
            tmp[col * 4 + row] =
                a->m[0 * 4 + row] * b->m[col * 4 + 0] +
                a->m[1 * 4 + row] * b->m[col * 4 + 1] +
                a->m[2 * 4 + row] * b->m[col * 4 + 2] +
                a->m[3 * 4 + row] * b->m[col * 4 + 3];
        }
    }
    memcpy(dest->m, tmp, sizeof(tmp));
    return dest;
}

static inline Matrix4f* Matrix4f_LookAt(Matrix4f* dest, float xFrom, float yFrom, float zFrom, float xTo, float yTo, float zTo, float xUp, float yUp, float zUp) {

    float magUp = sqrt(xUp * xUp + yUp * yUp + zUp * zUp);
    xUp /= magUp;
    yUp /= magUp;
    zUp /= magUp;

    float xLook = xTo - xFrom;
    float yLook = yTo - yFrom;
    float zLook = zTo - zFrom;
    float magLook = sqrt(xLook * xLook + yLook * yLook + zLook * zLook);
    xLook /= magLook;
    yLook /= magLook;
    zLook /= magLook;

    // normalised cross product between Up and Look
    float xRight = yUp * zLook - zUp * yLook;
    float yRight = zUp * xLook - xUp * zLook;
    float zRight = xUp * yLook - yUp * xLook;
    float magRight = sqrt(xRight * xRight + yRight * yRight + zRight * zRight);
    xRight /= magRight;
    yRight /= magRight;
    zRight /= magRight;

    // normalised cross product between Look and Right
    xUp = yLook * zRight - zLook * yRight;
    yUp = zLook * xRight - xLook * zRight;
    zUp = xLook * yRight - yLook * xRight;
    magUp = sqrt(xUp * xUp + yUp * yUp + zUp * zUp);
    xUp /= magUp;
    yUp /= magUp;
    zUp /= magUp;

    float x, y, z;
    x = xFrom * xRight + yFrom * yRight + zFrom * zRight;
    y = xFrom * xUp + yFrom * yUp + zFrom * zUp;
    z = xFrom * xLook + yFrom * yLook + zFrom * zLook;

    dest->m[Matrix_getIndex(0, 0)] = xRight;
    dest->m[Matrix_getIndex(1, 0)] = xUp;
    dest->m[Matrix_getIndex(2, 0)] = xLook;

    dest->m[Matrix_getIndex(0, 1)] = yRight;
    dest->m[Matrix_getIndex(1, 1)] = yUp;
    dest->m[Matrix_getIndex(2, 1)] = yLook;

    dest->m[Matrix_getIndex(0, 2)] = zRight;
    dest->m[Matrix_getIndex(1, 2)] = zUp;
    dest->m[Matrix_getIndex(2, 2)] = zLook;

    dest->m[Matrix_getIndex(0, 3)] = -x;
    dest->m[Matrix_getIndex(1, 3)] = -y;
    dest->m[Matrix_getIndex(2, 3)] = -z;

    return dest;
}

static inline Matrix4f* Matrix4f_Orthographic(Matrix4f* dest, float width, float height, float zfar, float znear) {

    memset(dest->m, 0, sizeof(dest->m));
    dest->m[Matrix_getIndex(0,0)] = 2.0f / width;
    dest->m[Matrix_getIndex(1,1)] = 2.0f / height;
    dest->m[Matrix_getIndex(2,2)] = 1.0f / (zfar - znear);
    dest->m[Matrix_getIndex(3,3)] = 1.0f;

    dest->m[Matrix_getIndex(2,3)] = znear / (znear - zfar);

    return dest;
}

// ===[ Orthographic Projection ]===

// Post-multiply orthographic projection onto dest: dest = dest * ortho(l, r, b, t, n, f)
static inline Matrix4f* Matrix4f_ortho(Matrix4f* dest, float left, float right, float bottom, float top, float zNear, float zFar) {
    Matrix4f ortho;
    memset(ortho.m, 0, sizeof(ortho.m));
    ortho.m[0] = 2.0f / (right - left);
    ortho.m[5] = 2.0f / (top - bottom);
    ortho.m[10] = -2.0f / (zFar - zNear);
    ortho.m[12] = -(right + left) / (right - left);
    ortho.m[13] = -(top + bottom) / (top - bottom);
    ortho.m[14] = -(zFar + zNear) / (zFar - zNear);
    ortho.m[15] = 1.0f;
    return Matrix4f_multiply(dest, dest, &ortho);
}

// ===[ Translate ]===

// Post-multiply translation onto dest: dest = dest * T(x, y, z)
// Optimized: only column 3 changes when post-multiplying a translation matrix
static inline Matrix4f* Matrix4f_translate(Matrix4f* dest, float x, float y, float z) {
    dest->m[12] += dest->m[0] * x + dest->m[4] * y + dest->m[8] * z;
    dest->m[13] += dest->m[1] * x + dest->m[5] * y + dest->m[9] * z;
    dest->m[14] += dest->m[2] * x + dest->m[6] * y + dest->m[10] * z;
    dest->m[15] += dest->m[3] * x + dest->m[7] * y + dest->m[11] * z;
    return dest;
}

// ===[ Rotate Z ]===

// Post-multiply Z-axis rotation onto dest: dest = dest * Rz(angleRadians)
static inline Matrix4f* Matrix4f_rotateZ(Matrix4f* dest, float angleRadians) {
    float c = cosf(angleRadians);
    float s = sinf(angleRadians);
    // Columns 0 and 1 are affected: new_col0 = col0*c + col1*s, new_col1 = col0*(-s) + col1*c
    for (int row = 0; 4 > row; row++) {
        float a0 = dest->m[0 * 4 + row];
        float a1 = dest->m[1 * 4 + row];
        dest->m[0 * 4 + row] = a0 * c + a1 * s;
        dest->m[1 * 4 + row] = a0 * (-s) + a1 * c;
    }
    return dest;
}

// ===[ Scale ]===

// Post-multiply scale onto dest: dest = dest * S(sx, sy, sz)
// Optimized: scales each column directly
static inline Matrix4f* Matrix4f_scale(Matrix4f* dest, float sx, float sy, float sz) {
    for (int row = 0; 4 > row; row++) {
        dest->m[0 * 4 + row] *= sx;
        dest->m[1 * 4 + row] *= sy;
        dest->m[2 * 4 + row] *= sz;
    }
    return dest;
}

// ===[ Set Transform 2D ]===

// Directly sets dest to a combined translate * rotateZ * scale matrix (no post-multiply)
// Equivalent to: identity -> translate(x, y, 0) -> rotateZ(angleRad) -> scale(sx, sy, 1)
static inline Matrix4f* Matrix4f_setTransform2D(Matrix4f* dest, float x, float y, float sx, float sy, float angleRad) {
    float c = cosf(angleRad);
    float s = sinf(angleRad);

    // Column 0: rotated+scaled X axis
    dest->m[0] = c * sx;
    dest->m[1] = s * sx;
    dest->m[2] = 0.0f;
    dest->m[3] = 0.0f;

    // Column 1: rotated+scaled Y axis
    dest->m[4] = -s * sy;
    dest->m[5] = c * sy;
    dest->m[6] = 0.0f;
    dest->m[7] = 0.0f;

    // Column 2: Z axis (identity for 2D)
    dest->m[8] = 0.0f;
    dest->m[9] = 0.0f;
    dest->m[10] = 1.0f;
    dest->m[11] = 0.0f;

    // Column 3: translation
    dest->m[12] = x;
    dest->m[13] = y;
    dest->m[14] = 0.0f;
    dest->m[15] = 1.0f;

    return dest;
}

// ===[ Camera View-Projection ]===

// Mirrors a world -> clip matrix vertically in NDC (negates the clip-space Y row).
// Renderers whose framebuffer is stored opposite to GameMaker's top-down convention apply this locally before upload.
static inline void Matrix4f_flipClipY(Matrix4f* m) {
    m->m[1] = -m->m[1];
    m->m[5] = -m->m[5];
    m->m[9] = -m->m[9];
    m->m[13] = -m->m[13];
}

// Builds the world -> clip (NDC) transform for a 2D camera that shows the room rectangle [left, left+width] x [top, top+height] in GameMaker's
// Y-down coordinate space, optionally rotated by angleDeg counter-clockwise about the view center (matching GML view_angle).
static inline Matrix4f* Matrix4f_viewProjection(Matrix4f* dest, float left, float top, float width, float height, float angleDeg) {
    Matrix4f_identity(dest);
    Matrix4f_ortho(dest, left, left + width, top + height, top, -1.0f, 1.0f);

    if (angleDeg != 0.0f) {
        // Rotate the world opposite the camera, about the view center, to spin the camera by angleDeg.
        float cx = left + width * 0.5f;
        float cy = top + height * 0.5f;
        Matrix4f rot;
        Matrix4f_identity(&rot);
        Matrix4f_translate(&rot, cx, cy, 0.0f);
        Matrix4f_rotateZ(&rot, -angleDeg * (float) M_PI / 180.0f);
        Matrix4f_translate(&rot, -cx, -cy, 0.0f);
        Matrix4f_multiply(dest, dest, &rot);
    }
    return dest;
}

// ===[ GUI Projection ]===

// Ortho for the GUI layer that preserves the guiW:guiH aspect inside a viewportW:viewportH viewport, centering
// (pillarbox/letterbox) instead of stretching. Identity to a plain ortho(0,guiW,guiH,0) when the aspects match.
static inline Matrix4f* Matrix4f_guiProjection(Matrix4f* dest, float guiW, float guiH, float viewportW, float viewportH) {
    float left = 0.0f, right = guiW, top = 0.0f, bottom = guiH;
    if (guiW > 0.0f && guiH > 0.0f && viewportW > 0.0f && viewportH > 0.0f) {
        float viewAspect = viewportW / viewportH;
        float guiAspect = guiW / guiH;
        if (viewAspect > guiAspect) {
            float margin = (guiH * viewAspect - guiW) * 0.5f;
            left = -margin;
            right = guiW + margin;
        } else if (viewAspect < guiAspect) {
            float margin = (guiW / viewAspect - guiH) * 0.5f;
            top = -margin;
            bottom = guiH + margin;
        }
    }
    Matrix4f_identity(dest);
    Matrix4f_ortho(dest, left, right, bottom, top, -1.0f, 1.0f);
    return dest;
}

// ===[ Transform Point ]===

// Transform a 2D point (x, y) through the matrix (w=1), writing results to outX, outY
// Useful for CPU-side vertex transforms (e.g. PS2/gsKit software rendering)
static inline void Matrix4f_transformPoint(const Matrix4f* mat, float x, float y, float* outX, float* outY) {
    *outX = mat->m[0] * x + mat->m[4] * y + mat->m[12];
    *outY = mat->m[1] * x + mat->m[5] * y + mat->m[13];
}

// Returns true if the matrix is a 2D affinte transformation in the xy plane.
static inline bool Matrix4f_isAffine2D(const Matrix4f* mat) {
    const float eps = 1e-6f;
    return eps > fabsf(mat->m[3]) && eps > fabsf(mat->m[7]) && eps > fabsf(mat->m[11]) && eps > fabsf(mat->m[15] - 1.0f) // no perspective row
        && eps > fabsf(mat->m[8]) && eps > fabsf(mat->m[9]); // no z coupling into x/y
}


// Helper function for a 3x3 determinant. You SHOULDN'T be using this.
static inline float Matrix3f_determinant(const Matrix4f *mat) {
    return
        + (mat->m[Matrix_getIndex(0,0)]*mat->m[Matrix_getIndex(1,1)]*mat->m[Matrix_getIndex(2,2)])
        + (mat->m[Matrix_getIndex(0,1)]*mat->m[Matrix_getIndex(1,2)]*mat->m[Matrix_getIndex(2,0)])
        + (mat->m[Matrix_getIndex(0,2)]*mat->m[Matrix_getIndex(1,0)]*mat->m[Matrix_getIndex(2,1)])

        - (mat->m[Matrix_getIndex(0,2)]*mat->m[Matrix_getIndex(1,1)]*mat->m[Matrix_getIndex(2,0)])
        - (mat->m[Matrix_getIndex(0,1)]*mat->m[Matrix_getIndex(1,0)]*mat->m[Matrix_getIndex(2,2)])
        - (mat->m[Matrix_getIndex(0,0)]*mat->m[Matrix_getIndex(1,2)]*mat->m[Matrix_getIndex(2,1)]);

}
// Computes the determinant of a 4x4 matrix. 
// I hope your math teacher taught you Leibniz's formula because I'm NOT gonna be explaining it AT ALL.
static inline float Matrix4f_determinant(const Matrix4f *mat) {
    float accumulator = 0;
    Matrix4f col;
    for (int x = 0, sign = 1; x < 4; x++, sign = -sign)
    {
        float term = sign * mat->m[Matrix_getIndex(0, x)];

        // Collect our the intended sub-determinant.
        for (int subXP = 0; subXP < 3; subXP++) {
            for (int subYP = 0; subYP < 3; subYP++) {
                int subX = subXP >= x ? subXP + 1 : subXP;
                int subY = subYP + 1;
                col.m[Matrix_getIndex(subYP, subXP)] = mat->m[Matrix_getIndex(subY, subX)];
            }
        }
        accumulator += term * Matrix3f_determinant(&col);
    }
    return accumulator;
}

// Computes a matrix's inverse and returns true/false if it even exists
static inline bool Matrix4f_inverse(Matrix4f *inv, const Matrix4f *mat) {
    float determinant = Matrix4f_determinant(mat);

    // TODO: have an epsilon.
    if (determinant == 0) {
        return false;
    }
    float invDet = 1.f / determinant;

    inv->m[0] = mat->m[5]  * mat->m[10] * mat->m[15] -
             mat->m[5]  * mat->m[11] * mat->m[14] -
             mat->m[9]  * mat->m[6]  * mat->m[15] +
             mat->m[9]  * mat->m[7]  * mat->m[14] +
             mat->m[13] * mat->m[6]  * mat->m[11] -
             mat->m[13] * mat->m[7]  * mat->m[10];

    inv->m[4] = -mat->m[4]  * mat->m[10] * mat->m[15] +
              mat->m[4]  * mat->m[11] * mat->m[14] +
              mat->m[8]  * mat->m[6]  * mat->m[15] -
              mat->m[8]  * mat->m[7]  * mat->m[14] -
              mat->m[12] * mat->m[6]  * mat->m[11] +
              mat->m[12] * mat->m[7]  * mat->m[10];

    inv->m[8] = mat->m[4]  * mat->m[9] * mat->m[15] -
             mat->m[4]  * mat->m[11] * mat->m[13] -
             mat->m[8]  * mat->m[5] * mat->m[15] +
             mat->m[8]  * mat->m[7] * mat->m[13] +
             mat->m[12] * mat->m[5] * mat->m[11] -
             mat->m[12] * mat->m[7] * mat->m[9];

    inv->m[12] = -mat->m[4]  * mat->m[9] * mat->m[14] +
               mat->m[4]  * mat->m[10] * mat->m[13] +
               mat->m[8]  * mat->m[5] * mat->m[14] -
               mat->m[8]  * mat->m[6] * mat->m[13] -
               mat->m[12] * mat->m[5] * mat->m[10] +
               mat->m[12] * mat->m[6] * mat->m[9];

    inv->m[1] = -mat->m[1]  * mat->m[10] * mat->m[15] +
              mat->m[1]  * mat->m[11] * mat->m[14] +
              mat->m[9]  * mat->m[2] * mat->m[15] -
              mat->m[9]  * mat->m[3] * mat->m[14] -
              mat->m[13] * mat->m[2] * mat->m[11] +
              mat->m[13] * mat->m[3] * mat->m[10];

    inv->m[5] = mat->m[0]  * mat->m[10] * mat->m[15] -
             mat->m[0]  * mat->m[11] * mat->m[14] -
             mat->m[8]  * mat->m[2] * mat->m[15] +
             mat->m[8]  * mat->m[3] * mat->m[14] +
             mat->m[12] * mat->m[2] * mat->m[11] -
             mat->m[12] * mat->m[3] * mat->m[10];

    inv->m[9] = -mat->m[0]  * mat->m[9] * mat->m[15] +
              mat->m[0]  * mat->m[11] * mat->m[13] +
              mat->m[8]  * mat->m[1] * mat->m[15] -
              mat->m[8]  * mat->m[3] * mat->m[13] -
              mat->m[12] * mat->m[1] * mat->m[11] +
              mat->m[12] * mat->m[3] * mat->m[9];

    inv->m[13] = mat->m[0]  * mat->m[9] * mat->m[14] -
              mat->m[0]  * mat->m[10] * mat->m[13] -
              mat->m[8]  * mat->m[1] * mat->m[14] +
              mat->m[8]  * mat->m[2] * mat->m[13] +
              mat->m[12] * mat->m[1] * mat->m[10] -
              mat->m[12] * mat->m[2] * mat->m[9];

    inv->m[2] = mat->m[1]  * mat->m[6] * mat->m[15] -
             mat->m[1]  * mat->m[7] * mat->m[14] -
             mat->m[5]  * mat->m[2] * mat->m[15] +
             mat->m[5]  * mat->m[3] * mat->m[14] +
             mat->m[13] * mat->m[2] * mat->m[7] -
             mat->m[13] * mat->m[3] * mat->m[6];

    inv->m[6] = -mat->m[0]  * mat->m[6] * mat->m[15] +
              mat->m[0]  * mat->m[7] * mat->m[14] +
              mat->m[4]  * mat->m[2] * mat->m[15] -
              mat->m[4]  * mat->m[3] * mat->m[14] -
              mat->m[12] * mat->m[2] * mat->m[7] +
              mat->m[12] * mat->m[3] * mat->m[6];

    inv->m[10] = mat->m[0]  * mat->m[5] * mat->m[15] -
              mat->m[0]  * mat->m[7] * mat->m[13] -
              mat->m[4]  * mat->m[1] * mat->m[15] +
              mat->m[4]  * mat->m[3] * mat->m[13] +
              mat->m[12] * mat->m[1] * mat->m[7] -
              mat->m[12] * mat->m[3] * mat->m[5];

    inv->m[14] = -mat->m[0]  * mat->m[5] * mat->m[14] +
               mat->m[0]  * mat->m[6] * mat->m[13] +
               mat->m[4]  * mat->m[1] * mat->m[14] -
               mat->m[4]  * mat->m[2] * mat->m[13] -
               mat->m[12] * mat->m[1] * mat->m[6] +
               mat->m[12] * mat->m[2] * mat->m[5];

    inv->m[3] = -mat->m[1] * mat->m[6] * mat->m[11] +
              mat->m[1] * mat->m[7] * mat->m[10] +
              mat->m[5] * mat->m[2] * mat->m[11] -
              mat->m[5] * mat->m[3] * mat->m[10] -
              mat->m[9] * mat->m[2] * mat->m[7] +
              mat->m[9] * mat->m[3] * mat->m[6];

    inv->m[7] = mat->m[0] * mat->m[6] * mat->m[11] -
             mat->m[0] * mat->m[7] * mat->m[10] -
             mat->m[4] * mat->m[2] * mat->m[11] +
             mat->m[4] * mat->m[3] * mat->m[10] +
             mat->m[8] * mat->m[2] * mat->m[7] -
             mat->m[8] * mat->m[3] * mat->m[6];

    inv->m[11] = -mat->m[0] * mat->m[5] * mat->m[11] +
               mat->m[0] * mat->m[7] * mat->m[9] +
               mat->m[4] * mat->m[1] * mat->m[11] -
               mat->m[4] * mat->m[3] * mat->m[9] -
               mat->m[8] * mat->m[1] * mat->m[7] +
               mat->m[8] * mat->m[3] * mat->m[5];

    inv->m[15] = mat->m[0] * mat->m[5] * mat->m[10] -
              mat->m[0] * mat->m[6] * mat->m[9] -
              mat->m[4] * mat->m[1] * mat->m[10] +
              mat->m[4] * mat->m[2] * mat->m[9] +
              mat->m[8] * mat->m[1] * mat->m[6] -
              mat->m[8] * mat->m[2] * mat->m[5];

    for (int i = 0; i < 16; i++) inv->m[i] *= invDet;
    return true;
}

#endif /* _BS_MATRIX_MATH_H_ */
