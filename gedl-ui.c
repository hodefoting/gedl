#define _BSD_SOURCE
#define _DEFAULT_SOURCE

#if 0
state:
  list of open filtes
  list of view setting per item
  currently edited on separate layer

  .-----.  .-----.
  |     |  |     |
  |     |  |     |
  '-----'  '-----'
  ================   clips live filtered by search
  ================   
  ================   
  ................
  ================ <- regardless of scroll,.

--------------------------------------------------

each clip show filename, followed by editable clip list for search
#endif

/*
   show all .edl .mp4 .ogv and .mpg files in a folder,
   show images of folder in a row..  permit editing stop-motion
   by zooming in enough to use individual frames decoded..
   
   toggle for generating cache or not
   implement instant reload on crash
     
drag in corners pans strip

tap and hold sets cursor position
followed by drag to select

having done drag select,.. the corresponding region should auto-play

an action available on playback deck shouldbe copy,. this would permit
creating a reference to another framesource file.

a too small drag is still just an insertion point selection, permitting
a single click from previous select drag (or tap to select existing seleciton/all),. permitting insertion.

 */


#include <stdio.h>
#include <mrg.h>
#include <gegl.h>
#include "gedl.h"

long babl_ticks (void);

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

int renderer_pos = 0;
int renderer_start = 0;
int renderer_end   = 0;
static void renderer_set_range (int start, int end)
{
  renderer_start = start;
  renderer_end = end;
  renderer_pos = 0;
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

static int playing  = 0;

float pan_x0 = 8;

//void rig_frame (int frame_no);
static void clicked_clip (MrgEvent *e, void *data1, void *data2)
{
  Clip *clip = data1;
  GeglEDL *edl = data2;
  edl->frame_no = e->x - pan_x0;
  edl->selection_start = edl->frame_no;
  edl->selection_end = edl->frame_no;
  active_clip = clip;
  mrg_queue_draw (e->mrg, NULL);
}
static void drag_clip (MrgEvent *e, void *data1, void *data2)
{
  GeglEDL *edl = data2;
  edl->selection_end = e->x - pan_x0;
  //active_clip = clip;
  mrg_queue_draw (e->mrg, NULL);
}

static void released_clip (MrgEvent *e, void *data1, void *data2)
{
  Clip *clip = data1;
  GeglEDL *edl = data2;
  edl->frame_no = e->x - pan_x0;
  active_clip = clip;
  mrg_queue_draw (e->mrg, NULL);
}

static void stop_playing (MrgEvent *event, void *data1, void *data2)
{
  playing = 0;
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
}

#if 0
static void start_playing (MrgEvent *event, void *data1, void *data2)
{
  playing = 0;
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
}
#endif

static void toggle_playing (MrgEvent *event, void *data1, void *data2)
{
  playing =  !playing;
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
}

static void extend_right (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  int sel_start, sel_end;

  gedl_get_selection (edl, &sel_start, &sel_end);
  if (edl->frame_no == sel_end)
  {
    sel_end ++;
    edl->frame_no ++;
  }
  else if (edl->frame_no == sel_start)
  {
    sel_start ++;
    edl->frame_no ++;
  }
  else
  {
    sel_start = sel_end = edl->frame_no;
    sel_end ++;
    edl->frame_no ++;
  }
  gedl_set_selection (edl, sel_start, sel_end);

  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
}

static void extend_left (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  int sel_start, sel_end;

  gedl_get_selection (edl, &sel_start, &sel_end);
  if (edl->frame_no == sel_end)
  {
    sel_end --;
    edl->frame_no --;
  }
  else if (edl->frame_no == sel_start)
  {
    sel_start --;
    edl->frame_no --;
  }
  else
  {
    sel_start = sel_end = edl->frame_no;
    sel_end --;
    edl->frame_no --;
  }
  gedl_set_selection (edl, sel_start, sel_end);

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
    edl->frame_no = active_clip->abs_start;
  }
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
  edl->frame=-1;
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
  edl->frame=-1;
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
    edl->frame_no = active_clip->abs_start;
  }
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
  edl->frame = -1;
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
}

static void save (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (edl->path)
    gedl_save_path (edl, edl->path);
  mrg_queue_draw (event->mrg, NULL);
}

static void set_range (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  int start, end;

  gedl_get_selection (edl, &start, &end);
  gedl_set_range (edl, start, end);
  mrg_queue_draw (event->mrg, NULL);
}

static void step_frame_back (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  stop_playing (event, data1, data2);
  edl->frame_no --;
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
}

static void step_frame (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  stop_playing (event, data1, data2);
  edl->frame_no ++;
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
}

static void clip_end_inc (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (active_clip)
    {
      active_clip->end++;
      edl->frame=-1;
      mrg_event_stop_propagate (event);
      mrg_queue_draw (event->mrg, NULL);
    }
}
static void clip_end_dec (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (active_clip)
    {
      active_clip->end--;
      edl->frame=-1;
      mrg_event_stop_propagate (event);
      mrg_queue_draw (event->mrg, NULL);
    }
}
static void clip_start_inc (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (active_clip)
    {
      active_clip->start++;
      edl->frame=-1; // hack - to dirty it
      mrg_event_stop_propagate (event);
      mrg_queue_draw (event->mrg, NULL);
    }
}
static void clip_start_dec (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (active_clip)
    {
      active_clip->start--;
      edl->frame=-1;
      mrg_event_stop_propagate (event);
      mrg_queue_draw (event->mrg, NULL);
    }
}
static void do_quit (MrgEvent *event, void *data1, void *data2)
{
  mrg_quit (event->mrg);
}

long last_frame = 0;

static int max_frame (GeglEDL *edl)
{
  GList *l;
  int t = 0;
  int start, end;

  gedl_get_range (edl, &start, &end);
  if (end)
    return end;

  for (l = edl->clips; l; l = l->next)
  {
    Clip *clip = l->data;
    t += clip_get_frames (clip);
  }

  return t;
}

void gedl_ui (Mrg *mrg, void *data);

void gedl_draw (Mrg *mrg, GeglEDL *edl, double x, double y)
{
  GList *l;
  cairo_t *cr = mrg_cr (mrg);
  int t = pan_x0;

  for (l = edl->clips; l; l = l->next)
  {
    Clip *clip = l->data;
    char thumb_path[PATH_MAX];
    sprintf (thumb_path, "%s.png", clip->path);

    cairo_rectangle (cr, t, y, clip_get_frames (clip), 40);
    cairo_set_source_rgba (cr, 0.1, 0.1, 0.1, 0.5);

    mrg_listen (mrg, MRG_PRESS, clicked_clip, clip, edl);
    mrg_listen (mrg, MRG_DRAG, drag_clip, clip, edl);
    mrg_listen (mrg, MRG_RELEASE, released_clip, clip, edl);

    int width, height;
    MrgImage *img = mrg_query_image (mrg, thumb_path, &width, &height);
    if (img && width > 0)
    {
      cairo_surface_t *surface = mrg_image_get_surface (img);
      cairo_save (cr);
      cairo_clip_preserve (cr);
      cairo_set_source_surface (cr, surface, t - clip->start, y);
      cairo_paint (cr);
      cairo_restore (cr);
    }
    else
    {
      cairo_fill_preserve (cr);
    }

    if (clip == active_clip)
      cairo_set_source_rgba (cr, 1, 1, 0.5, 1.0);
    else
      cairo_set_source_rgba (cr, 1, 1, 1, 0.5);
    cairo_stroke (cr);


    t += clip_get_frames (clip);
  }

  int start = 0, end = 0;
  gedl_get_selection (edl, &start, &end);
  cairo_rectangle (cr, start + pan_x0, y - 4, end - start, 40 + 4 * 2);
  cairo_set_source_rgba (cr, 0, 0, 0.11, 0.5);
  cairo_fill_preserve (cr);
  cairo_set_source_rgba (cr, 1, 1, 1, 0.5);
  cairo_stroke (cr);

  gedl_get_range (edl, &start, &end);
  cairo_rectangle (cr, start + pan_x0, y - 20, end - start, 10);
  cairo_set_source_rgba (cr, 0, 0.11, 0.0, 0.5);
  cairo_fill_preserve (cr);
  cairo_set_source_rgba (cr, 1, 1, 1, 0.5);
  cairo_stroke (cr);
}

void gedl_ui (Mrg *mrg, void *data)
{
  State *o = data;
  GeglEDL *edl = o->edl;
  float y = 500;
  cairo_t *cr = mrg_cr (mrg);

  if (playing)
    {
     int start, end;

     gedl_get_range (edl, &start, &end);

      edl->frame_no++;
      if (edl->frame_no > max_frame (edl))
      {
         edl->frame_no = 0;
         if (end)
           edl->frame_no = start;
      }
      mrg_queue_draw (mrg, NULL);
    }
  rig_frame (edl, edl->frame_no);
  mrg_gegl_blit (mrg, 1, 0, mrg_width (mrg), mrg_height (mrg),
                 result, 0,0,1.0, 1.0);

  gedl_draw (mrg, edl, 0.0, y);

  if (active_clip && 0)
    {
      mrg_printf (mrg, "%s %i %i%s", active_clip->path,
                                     active_clip->start, active_clip->end,
                                     active_clip->fade_out?" [fade]":"");

      if (active_clip->filter_graph)
        mrg_printf (mrg, " %s", active_clip->filter_graph);
    }

  cairo_rectangle (cr, edl->frame_no + pan_x0, y-10, 1, 60);
  cairo_set_source_rgba (cr,1,0,0,1);
  cairo_fill (cr);

  mrg_add_binding (mrg, "x", NULL, NULL, remove_clip, edl);
  mrg_add_binding (mrg, "d", NULL, NULL, duplicate_clip, edl);
  mrg_add_binding (mrg, "space", NULL, NULL, toggle_playing, edl);
  mrg_add_binding (mrg, "j", NULL, NULL, nav_left, edl);
  mrg_add_binding (mrg, "k", NULL, NULL, nav_right, edl);
  mrg_add_binding (mrg, ".", NULL, NULL, clip_end_inc, edl);
  mrg_add_binding (mrg, "f", NULL, NULL, toggle_fade, edl);
  mrg_add_binding (mrg, "s", NULL, NULL, save, edl);
  mrg_add_binding (mrg, "r", NULL, NULL, set_range, edl);
  mrg_add_binding (mrg, ",", NULL, NULL, clip_end_dec, edl);
  mrg_add_binding (mrg, "k", NULL, NULL, clip_start_inc, edl);
  mrg_add_binding (mrg, "l", NULL, NULL, clip_start_dec, edl);
  mrg_add_binding (mrg, "q", NULL, NULL, (void*)do_quit, mrg);
  mrg_add_binding (mrg, "shift-right", NULL, NULL, extend_right, edl);
  mrg_add_binding (mrg, "shift-left", NULL, NULL, extend_left, edl);
  mrg_add_binding (mrg, "right", NULL, NULL, step_frame, edl);
  mrg_add_binding (mrg, "left", NULL, NULL, step_frame_back, edl);
}

//static GThread *thread;

gpointer renderer_main (gpointer data)
{
  GeglEDL *edl = data;
  while (1)
  {
    if (renderer_pos < renderer_end)
    {
      char *cmd = g_strdup_printf ("./gedl %s /tmp/foo.mp4 -c -s %i -e %i", edl->path, renderer_start, renderer_end);
      system (cmd);
      renderer_pos = renderer_end;
    }
    else
    {
      g_usleep (1000);
    }
  }
  return NULL;
}

int gedl_ui_main (GeglEDL *edl);
int gedl_ui_main (GeglEDL *edl)
{
  Mrg *mrg = mrg_new (800, 600, NULL);
  State o = {NULL,};
  o.mrg = mrg;
  o.edl = edl;
  active_clip = edl->clips->data;
  edl->cache_flags = CACHE_TRY_ALL | CACHE_MAKE_ALL;
  renderer_set_range (0, 50);
  mrg_set_ui (mrg, gedl_ui, &o);

  //mrg_restarter_add_path (mrg, "gedl");

  mrg_main (mrg);
  gegl_exit ();

  return 0;
}
