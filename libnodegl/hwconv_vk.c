#include "hwconv.h"

int ngli_hwconv_init(struct hwconv *hwconv, struct ngl_ctx *ctx,
                     const struct image *dst_image,
                     const struct image_params *src_params)
{
    return 0;
}

int ngli_hwconv_convert_image(struct hwconv *hwconv, const struct image *image)
{
    return 0;
}

void ngli_hwconv_reset(struct hwconv *texconv)
{
}
