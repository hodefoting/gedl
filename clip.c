#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <gegl.h>
#include <gegl-audio-fragment.h>
#include "gedl.h"

Clip *clip_new (GeglEDL *edl)
{
  Clip *clip = g_malloc0 (sizeof (Clip));
  clip->edl = edl;
  clip->gegl = gegl_node_new ();
  clip->loader = gegl_node_new_child (clip->gegl, "operation", "gegl:ff-load", NULL);
  clip->proxy_loader = gegl_node_new_child (clip->gegl, "operation", "gegl:ff-load", NULL);
  clip->store_buf = gegl_node_new_child (clip->gegl, "operation", "gegl:buffer-sink", "buffer", &clip->buffer, NULL);
  gegl_node_link_many (clip->loader, clip->store_buf, NULL);
  g_mutex_init (&clip->mutex);

  return clip;
}
void clip_free (Clip *clip)
{
  if (clip->path)
    g_free (clip->path);
  clip->path = NULL;

  if (clip->buffer)
    g_object_unref (clip->buffer);
  clip->buffer = NULL;

  if (clip->gegl)
    g_object_unref (clip->gegl);
  clip->gegl = NULL;
  g_mutex_clear (&clip->mutex);
  g_free (clip);
}

void clip_set_path (Clip *clip, const char *in_path)
{
  char *path = NULL;
  if (in_path[0] == '/')
  {
    path = g_strdup (in_path);
  }
  else
  {
    if (clip->edl->parent_path)
      path = g_strdup_printf ("%s%s", clip->edl->parent_path, in_path);
    else
      path = g_strdup_printf ("%s", in_path);
  }

  if (clip->path && !strcmp (clip->path, path))
  {
    g_free (path);
    return;
  }

  if (clip->path)
    g_free (clip->path);
  clip->path = path;


  if (g_str_has_suffix (path, ".png") ||
      g_str_has_suffix (path, ".jpg") ||
      g_str_has_suffix (path, ".exr") ||
      g_str_has_suffix (path, ".EXR") ||
      g_str_has_suffix (path, ".PNG") ||
      g_str_has_suffix (path, ".JPG"))
   {
     g_object_set (clip->loader, "operation", "gegl:load", NULL);
     clip->static_source = 1;
   }
  else
   {
     g_object_set (clip->loader, "operation", "gegl:ff-load", NULL);
     clip->static_source = 0;
   }
}
int clip_get_start (Clip *clip)
{
  return clip->start;
}
int clip_get_end (Clip *clip)
{
  return clip->end;
}
int clip_get_frames (Clip *clip)
{
  int frames = clip_get_end (clip) - clip_get_start (clip) + 1;
  if (frames < 0) frames = 0;
  return frames;
}

void clip_set_start (Clip *clip, int start)
{
  clip->start = start;
}
void clip_set_end (Clip *clip, int end)
{
  clip->end = end;
}
void clip_set_range (Clip *clip, int start, int end)
{
  clip_set_start (clip, start);
  clip_set_end (clip, end);
}
void clip_set_full (Clip *clip, const char *path, int start, int end)
{
  clip_set_path (clip, path);
  clip_set_range (clip, start, end);
}
Clip *clip_new_full (GeglEDL *edl, const char *path, int start, int end)
{
  Clip *clip = clip_new (edl);
  clip_set_full (clip, path, start, end);
  return clip;
}
void clip_fade_set (Clip *clip, int do_fade_out)
{
  /*  should cancel any computations due to fade when cancelling it, and add them when fade is set
   */
}

const char *clip_get_path (Clip *clip)
{
  return clip->path;
}

static void clip_set_proxied (Clip *clip)
{
  if (clip->edl->use_proxies)
    {
      char *path = gedl_make_proxy_path (clip->edl, clip->path);
      gchar *old = NULL;
      gegl_node_get (clip->proxy_loader, "path", &old, NULL);

      if (!old || !strcmp (old, "") || !strcmp (path, old))
        gegl_node_set (clip->proxy_loader, "path", path, NULL);
      gegl_node_link_many (clip->proxy_loader, clip->store_buf, NULL);
      g_free (path);
    }
  else
    {
      gchar *old = NULL;
      gegl_node_get (clip->loader, "path", &old, NULL);
      if (!old || !strcmp (old, "") || !strcmp (clip->path, old))
        gegl_node_set (clip->loader, "path", clip->path, NULL);
      gegl_node_link_many (clip->loader, clip->store_buf, NULL);
    }
}

void clip_set_frame_no (Clip *clip, int frame_no)
{
  clip->clip_frame_no = frame_no;

  if (clip->clip_frame_no < 0)
    clip->clip_frame_no = 0;

  clip_set_proxied (clip);

  if (!clip_is_static_source (clip))
    {
      if (clip->edl->use_proxies)
        gegl_node_set (clip->proxy_loader, "frame", clip->clip_frame_no, NULL);
      else
        gegl_node_set (clip->loader, "frame", clip->clip_frame_no, NULL);
    }
}

int clip_is_static_source (Clip *clip)
{
  return clip->static_source;
}

void clip_fetch_audio (Clip *clip)
{
  int use_proxies = clip->edl->use_proxies;

  if (clip->audio)
    {
      g_object_unref (clip->audio);
      clip->audio = NULL;
    }

  if (clip_is_static_source (clip))
    clip->audio = NULL;
  else
    {
      if (use_proxies)
        gegl_node_get (clip->proxy_loader, "audio", &clip->audio, NULL);
      else
        gegl_node_get (clip->loader, "audio", &clip->audio, NULL);
    }
}

