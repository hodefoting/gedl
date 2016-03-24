/* This file is an image processing operation for GEGL
 *
 * GEGL is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * GEGL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GEGL; if not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright 2016 Øyvind Kolås <pippin@gimp.org>
 */

//#include <glib/gi18n-lib.h>
#define _(a) (a)

#ifdef GEGL_PROPERTIES

property_double (dampness, _("Dampness"), 0.95)
    description (_("The value represents the contribution of the past to the new frame."))
    value_range (0.0, 1.0)

#else

#define GEGL_OP_FILTER
#define GEGL_OP_NO_SOURCE
#define GEGL_OP_C_SOURCE tmedian.c

#include "gegl-op.h"

#define TEMP_BUFS 6

typedef struct
{
  GeglBuffer *acc[TEMP_BUFS];
} Priv;


static void
init (GeglProperties *o)
{
  Priv         *priv = (Priv*)o->user_data;
  GeglRectangle extent = {0,0,1024,1024};

  g_assert (priv == NULL);

  priv = g_new0 (Priv, 1);
  o->user_data = (void*) priv;

  for (int i=0;i< TEMP_BUFS;i++)
    priv->acc[i] = gegl_buffer_new (&extent, babl_format ("RGBA float"));
}

static void prepare (GeglOperation *operation)
{
  gegl_operation_set_format (operation, "output", babl_format ("RGBA float"));
}

#include <math.h>


static gboolean
process (GeglOperation       *operation,
         GeglBuffer          *input,
         GeglBuffer          *output,
         const GeglRectangle *result,
         gint                 level)
{
  GeglProperties *o;
  Priv           *p;

  o = GEGL_PROPERTIES (operation);
  p = (Priv*)o->user_data;
  if (p == NULL)
    init (o);
  p = (Priv*)o->user_data;

  {
    gint pixels = result->width * result->height;
    gfloat *acc[TEMP_BUFS];
    gfloat *buf;
    gint i;
    int x, y;
    int last_set[8192]={0,};
    for (i = 0; i < TEMP_BUFS; i++)
      acc[i] = g_new (gfloat, pixels * 4);
    buf = g_new (gfloat, pixels * 4);

    for (i = TEMP_BUFS-1; i > 0; i--)
    {
      gegl_buffer_get (p->acc[i-1], result, 1.0, babl_format ("RGBA float"), acc[i], GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);
      gegl_buffer_set (p->acc[i], result, 0, babl_format ("RGBA float"), acc[i], GEGL_AUTO_ROWSTRIDE);
    }

    gegl_buffer_get (input, result, 1.0, babl_format ("RGBA float"), acc[0], GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);
    gegl_buffer_get (input, result, 1.0, babl_format ("RGBA float"), buf, GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);
    gegl_buffer_set (p->acc[0], result, 0, babl_format ("RGBA float"), acc[0], GEGL_AUTO_ROWSTRIDE);
    for (i=0;i<pixels;i++)
      {
        gint c;


        for (c=0;c<4;c++)
        {
          int t;
          float avg[4] = {0,0,0,0};
          float avg2[4] = {0,0,0,0};

          for (t = 2; t < TEMP_BUFS; t++)
            avg[c] += acc[t][i*4+c];
          avg[c] /= (TEMP_BUFS);

          for (t = 2; t < TEMP_BUFS-1; t++)
            avg2[c] += acc[t][i*4+c];
          avg2[c] /= (TEMP_BUFS - 2 - 1);

#define ACC(a) acc[a][i*4+c]

          if (ACC(0) > avg[c]) 
            ACC(0) = avg2[c];
        }
      }

    i = 0;
    for (y = result->y; y < result->y + result->height; y++)
      for (x = result->x; x < result->x + result->width; x++)
      {
        int c;
        int diff = 0;
        for (c = 0; c < 3; c++)
          if (acc[0][i * 4 + c] != buf[i * 4 + c])
          {
            diff++;
          }
        if (diff > 4)
        {
          last_set[x] = 0;
          for (c = 0; c < 4; c++)
            buf[i * 4 + c] = acc[0][i * 4 + c] * 2;
        }
        else
        {
          last_set[x]++;
        }
        i++;
      }
    gegl_buffer_set (output, result, 0, babl_format ("RGBA float"), buf, GEGL_AUTO_ROWSTRIDE);
    for (i = 0; i < TEMP_BUFS; i++)
      g_free (acc[i]);
  }

  return  TRUE;
}

static void
finalize (GObject *object)
{
  GeglProperties *o = GEGL_PROPERTIES (object);

  if (o->user_data)
    {
      Priv *p = (Priv*)o->user_data;

      g_object_unref (p->acc);

      g_free (o->user_data);
      o->user_data = NULL;
    }

  G_OBJECT_CLASS (gegl_op_parent_class)->finalize (object);
}


static void
gegl_op_class_init (GeglOpClass *klass)
{
  GeglOperationClass       *operation_class;
  GeglOperationFilterClass *filter_class;

  G_OBJECT_CLASS (klass)->finalize = finalize;

  operation_class = GEGL_OPERATION_CLASS (klass);
  filter_class    = GEGL_OPERATION_FILTER_CLASS (klass);

  filter_class->process = process;
  operation_class->prepare = prepare;

  gegl_operation_class_set_keys (operation_class,
    "name",        "gegl:tmedian",
    "title",       _("Temporal difference"),
    "categories" , "video",
    "description", _("creates a mask with difference between frames"),
    NULL);
}

#endif
