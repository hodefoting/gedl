#define _BSD_SOURCE
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <mrg.h>
#include <gegl.h>
#include "gedl.h"

void frob_fade (void*);
static unsigned char *copy_buf = NULL;
static int copy_buf_len = 0;

extern GeglNode *result;

static void mrg_gegl_blit (Mrg *mrg,
                          float x0, float y0,
                          float width, float height,
                          GeglNode *node,
                          float u, float v,
                          float scale,
                           float preview_multiplier)
{
  float fake_factor = preview_multiplier;
  GeglRectangle bounds;

  cairo_t *cr = mrg_cr (mrg);
  cairo_surface_t *surface = NULL;

  if (!node)
    return;

  bounds = gegl_node_get_bounding_box (node);

  if (width == -1 && height == -1)
  {
    width  = bounds.width;
    height = bounds.height;
  }

  if (width == -1)
    width = bounds.width * height / bounds.height;
  if (height == -1)
    height = bounds.height * width / bounds.width;

  width /= fake_factor;
  height /= fake_factor;
  u /= fake_factor;
  v /= fake_factor;
 
  if (copy_buf_len < width * height * 4)
  {
    if (copy_buf)
      free (copy_buf);
    copy_buf_len = width * height * 4;
    copy_buf = malloc (copy_buf_len);
  }
  {
    static int foo = 0;
    unsigned char *buf = copy_buf;
    GeglRectangle roi = {u, v, width, height};
    static const Babl *fmt = NULL;

foo++;
    if (!fmt) fmt = babl_format ("cairo-RGB24");
    gegl_node_blit (node, scale / fake_factor, &roi, fmt, buf, width * 4, 
         GEGL_BLIT_DEFAULT);
  surface = cairo_image_surface_create_for_data (buf, CAIRO_FORMAT_RGB24, width, height, width * 4);
  }

  cairo_save (cr);
  cairo_surface_set_device_scale (surface, 1.0/fake_factor, 1.0/fake_factor);
  
  width *= fake_factor;
  height *= fake_factor;
  u *= fake_factor;
  v *= fake_factor;

  cairo_rectangle (cr, x0, y0, width, height);

  cairo_clip (cr);
  cairo_translate (cr, x0 * fake_factor, y0 * fake_factor);
  cairo_pattern_set_filter (cairo_get_source (cr), CAIRO_FILTER_NEAREST);
  cairo_set_source_surface (cr, surface, 0, 0);
   
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_paint (cr);
  cairo_surface_destroy (surface);
  cairo_restore (cr);
}

typedef struct _State State;
struct _State {
  void      (*ui) (Mrg *mrg, void *state);
  Mrg        *mrg;
  GeglEDL    *edl;

  char       *path;
  char       *save_path;
};


Clip *active_clip = NULL;

static int frame_no = 0;
static int playing = 0;

void rig_frame (int frame_no);
static void clicked_clip (MrgEvent *e, void *data1, void *data2)
{
  Clip *clip = data1;
  frame_no = e->x;
  active_clip = clip;
  rig_frame (frame_no);
  mrg_queue_draw (e->mrg, NULL);
}

static void released_clip (MrgEvent *e, void *data1, void *data2)
{
  Clip *clip = data1;
  frame_no = e->x;
  rig_frame (frame_no);
  active_clip = clip;
  mrg_queue_draw (e->mrg, NULL);
}

static void toggle_playing (MrgEvent *event, void *data1, void *data2)
{
  playing =  !playing;
  //mrg_set_fullscreen (event->mrg, !mrg_is_fullscreen (event->mrg));
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
}

static void nav_right (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (!active_clip)
    return;
  {
    GList *iter = g_list_find (edl->clips, active_clip);
    if (iter) iter = iter->next;
    if (iter) active_clip = iter->data;
  }
  rig_frame (frame_no);
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
}

static void remove_clip (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (!active_clip)
    return;
  {
    GList *iter = g_list_find (edl->clips, active_clip);
    if (iter) iter = iter->next;
    if (!iter)
      return;
    edl->clips = g_list_remove (edl->clips, active_clip);
    if (iter) active_clip = iter->data;
    frob_fade (active_clip);
  }
  rig_frame (frame_no);
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
}
static void duplicate_clip (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (!active_clip)
    return;
  {
    GList *iter = g_list_find (edl->clips, active_clip);
    Clip *clip = clip_new_full (active_clip->path, active_clip->start, active_clip->end);
    edl->clips = g_list_insert_before (edl->clips, iter, clip);
    frob_fade (active_clip);
    if (active_clip->filter_graph) clip->filter_graph = g_strdup (active_clip->filter_graph);
    active_clip = clip;
    frob_fade (active_clip);
  }
  rig_frame (frame_no);
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
}

static void nav_left (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (!active_clip)
    return;
  {
    GList *iter = g_list_find (edl->clips, active_clip);
    if (iter) iter = iter->prev;
    if (iter) active_clip = iter->data;
  }
  rig_frame (frame_no);
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
}
static void toggle_fade (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (!active_clip)
    return;
  active_clip->fade_out = !active_clip->fade_out;
  frob_fade (active_clip);
  {
    GList *iter = g_list_find (edl->clips, active_clip);
    if (iter) iter = iter->next;
    if (iter) {
      Clip *clip2 = iter->data;
      clip2->fade_in = active_clip->fade_out;
      frob_fade (clip2);
    }
  }
  if (!active_clip->fade_out)
    active_clip->fade_pad_end = 0;
  frame_no --;
  rig_frame (frame_no);
  frame_no ++;
  rig_frame (frame_no);
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
}


static void step_frame (MrgEvent *event, void *data1, void *data2)
{
  //GeglEDL *edl = data1;
  frame_no ++;
  rig_frame (frame_no);
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
}

static void clip_end_inc (MrgEvent *event, void *data1, void *data2)
{
  if (active_clip)
    {
      active_clip->end++;
      mrg_event_stop_propagate (event);
      mrg_queue_draw (event->mrg, NULL);
    }
}
static void clip_end_dec (MrgEvent *event, void *data1, void *data2)
{
  if (active_clip)
    {
      active_clip->end--;
      mrg_event_stop_propagate (event);
      mrg_queue_draw (event->mrg, NULL);
    }
}
static void clip_start_inc (MrgEvent *event, void *data1, void *data2)
{
  if (active_clip)
    {
      active_clip->start++;
      mrg_event_stop_propagate (event);
      mrg_queue_draw (event->mrg, NULL);
    }
}
static void clip_start_dec (MrgEvent *event, void *data1, void *data2)
{
  if (active_clip)
    {
      active_clip->start--;
      mrg_event_stop_propagate (event);
      mrg_queue_draw (event->mrg, NULL);
    }
}


void gedl_ui (Mrg *mrg, void *data);
void gedl_ui (Mrg *mrg, void *data)
{
  State *o = data;
  GeglEDL *edl = o->edl;
  GList *l;
  float y = 50;
  int t = 0;
  //static int frame_no = 0;
    cairo_t *cr = mrg_cr (mrg);
  mrg_gegl_blit (mrg, 0, 0, mrg_width (mrg), mrg_height (mrg),
                 result, 0,0,1.0, 1.0);

  for (l = edl->clips; l; l = l->next)
  {
    Clip *clip = l->data;
    if(0)mrg_printf (mrg, "%s %i %i\n", clip->path, clip->start, clip->end);
    cairo_rectangle (cr, t, y, clip_get_frames (clip), 40);
    cairo_set_source_rgba (cr, 0.1, 0.1, 0.1, 0.5);

    mrg_listen (mrg, MRG_PRESS, clicked_clip, clip, NULL);
    mrg_listen (mrg, MRG_DRAG, clicked_clip, clip, NULL);
    mrg_listen (mrg, MRG_RELEASE, released_clip, clip, NULL);

    cairo_stroke_preserve (cr);
    if (clip == active_clip)
    cairo_set_source_rgba (cr, 1, 1, 0.5, 1.0);
    else
    cairo_set_source_rgba (cr, 1, 1, 1, 0.5);
    cairo_fill (cr);
    t += clip_get_frames (clip);
    //mrg_printf (mrg, "%s %i %i\n", clip->path, clip->start, clip->end);
  }
    mrg_printf (mrg, "frame: %i\n", frame_no);

    if (active_clip)
    {
       mrg_printf (mrg, "%s %i %i", active_clip->path, active_clip->start, active_clip->end);
       if (active_clip->filter_graph)
         mrg_printf (mrg, " %s", active_clip->filter_graph);
    }
    cairo_rectangle (cr, frame_no, y-10, 1, 60);
    cairo_set_source_rgba (cr,1,0,0,1);
    cairo_fill (cr);
    if (playing) {
      frame_no++;
      rig_frame (frame_no);
      mrg_queue_draw (mrg, NULL);
    }

  mrg_add_binding (mrg, "x", NULL, NULL, remove_clip, edl);
  mrg_add_binding (mrg, "d", NULL, NULL, duplicate_clip, edl);
  mrg_add_binding (mrg, "space", NULL, NULL, toggle_playing, NULL);
  mrg_add_binding (mrg, "left", NULL, NULL, nav_left, edl);
  mrg_add_binding (mrg, "right", NULL, NULL, nav_right, edl);
  mrg_add_binding (mrg, ".", NULL, NULL, clip_end_inc, NULL);
  mrg_add_binding (mrg, "f", NULL, NULL, toggle_fade, edl);
  mrg_add_binding (mrg, ",", NULL, NULL, clip_end_dec, NULL);
  mrg_add_binding (mrg, "k", NULL, NULL, clip_start_inc, NULL);
  mrg_add_binding (mrg, "l", NULL, NULL, clip_start_dec, NULL);
  mrg_add_binding (mrg, "q", NULL, NULL, (void*)mrg_quit, NULL);
  mrg_add_binding (mrg, "/", NULL, NULL, step_frame, NULL);
}


int gedl_ui_main (GeglEDL *edl);
int gedl_ui_main (GeglEDL *edl)
{
  Mrg *mrg = mrg_new (1024, 768, NULL);
  State o = {NULL,};
  o.mrg = mrg;
  o.edl = edl;
  active_clip = edl->clips->data;
  mrg_set_ui (mrg, gedl_ui, &o);
  mrg_main (mrg);
  gegl_exit ();

  return 0;
}

