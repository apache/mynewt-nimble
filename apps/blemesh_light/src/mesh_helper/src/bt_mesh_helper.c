/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "mesh/mesh.h"
#include "mesh_helper/include/bt_mesh_helper.h"

void
rgb_to_rgbw(uint16_t *red, uint16_t *green, uint16_t *blue, uint16_t *white)
{
    uint16_t temp_white;

    /* If Maximum Value Of RGB Is Zero, White Is Also Zero */
    if (0 == MAX_OF_3(*red,*green,*blue)) {
        *white = 0;
        return;
    }

    /* Else, Compute White */
    temp_white = (uint16_t)(((uint32_t) MIN_OF_3(*red,*green,*blue) * (*red + *green + *blue)) / (MAX_OF_3(*red,*green,*blue) * 3));
    *red   += temp_white;
    *green += temp_white;
    *blue  += temp_white;

    *white = MIN_OF_3(*red, *green, *blue);
    *white = (*white > 1000) ? 1000 : *white;

    *red   -= *white;
    *green -= *white;
    *blue  -= *white;
}

static double
hue_to_rgb (double v1, double v2, double vH)
{
    if (vH < 0) vH += 1;
    if (vH > 1) vH -= 1;
    if ((6 * vH) < 1) return (v1 + (v2 - v1) * 6 * vH);
    if ((2 * vH) < 1) return (v2);
    if ((3 * vH) < 2) return (v1 + (v2 - v1) * ((2.0f/3) - vH) * 6);

    return v1;
}

void
hsl_to_rgbw(struct hsl_color_format *hsl, struct rgbw_color_format *rgbw)
{
    if (hsl == NULL || rgbw == NULL)
        return;

    /* Rescaling hue range from 0-65535 to 0-360 */
    double h = (double) (hsl->hue / 182.041666667f) / 360.0f;

    /* Rescaling saturation range from 0-65535 to 0.0-1.0 */
    double s = (double) hsl->saturation / 65535.0f;

    /* Rescaling lightness range from 0-65535 to 0.0-1.0 */
    double l = (double) hsl->lightness / 65535.0f;

    if (s == 0) {
        rgbw->red = rgbw->green = rgbw->blue = (uint16_t) (l * 1000.0f);
    } else {
        float v1 = 0, v2 = 0;

        if (l < 0.5)
            v2 = l * (1 + s);
        else
            v2 = (l + s) - (s * l);

        v1 = 2 * l - v2;

        rgbw->red = (uint16_t) (1000.0f * hue_to_rgb(v1, v2, h + (1.0f/3)));
        rgbw->green = (uint16_t) (1000.0f * hue_to_rgb(v1, v2, h));
        rgbw->blue = (uint16_t) (1000.0f * hue_to_rgb(v1, v2, h - (1.0f/3)));
        rgbw->white = 0;

        rgb_to_rgbw(&rgbw->red, &rgbw->green, &rgbw->blue, &rgbw->white);
    }
}


