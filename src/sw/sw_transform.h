#ifndef _SW_TRANSFORM_H
#define _SW_TRANSFORM_H

static void swrTransformPosIfNeeded(SWRenderer* swr, float* dx, float* dy)
{
    if (dx) {
        *dx -= swr->viewX;
        *dx *= swr->scaleX;
        *dx += swr->portX;
    }
    if (dy) {
        *dy -= swr->viewY;
        *dy *= swr->scaleY;
        *dy += swr->portY;
    }
}

static void swrTransformSizeIfNeeded(SWRenderer* swr, float* dx, float* dy)
{
    if (dx) *dx *= swr->scaleX;
    if (dy) *dy *= swr->scaleY;
}

static void swrTransformPosIntIfNeeded(SWRenderer* swr, int32_t* dx, int32_t* dy)
{
    if (dx) {
        *dx -= swr->viewX;
        *dx = (int)(*dx * swr->scaleX);
        *dx += swr->portX;
    }
    if (dy) {
        *dy -= swr->viewY;
        *dy = (int)(*dy * swr->scaleY);
        *dy += swr->portY;
    }
}

static void swrTransformSizeIntIfNeeded(SWRenderer* swr, int32_t* dx, int32_t* dy)
{
    if (dx) *dx = (int)(*dx * swr->scaleX);
    if (dy) *dy = (int)(*dy * swr->scaleY);
}

static void swrReverseTransformSizeIfNeeded(SWRenderer* swr, float* dx, float* dy)
{
    if (dx) *dx = *dx / swr->scaleX;
    if (dy) *dy = *dy / swr->scaleY;
}

static void swrReverseTransformSizeIntIfNeeded(SWRenderer* swr, int32_t* dx, int32_t* dy)
{
    if (dx) *dx = (int)(*dx / swr->scaleX);
    if (dy) *dy = (int)(*dy / swr->scaleY);
}

#endif//_SW_TRANSFORM_H
