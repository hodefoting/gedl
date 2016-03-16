/*

todo: prime cache frames when navigating clips, shared with raw edits of same
frames appearing in timeline

split rendering to separate thread  ... also split rendering of selecting in
clip-db to the separate thread.

 */

#define _BSD_SOURCE
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <mrg.h>
#include <gegl.h>
#include "gedl.h"

static GThread *thread;
long babl_ticks (void);

void frob_fade (void*);
static unsigned char *copy_buf = NULL;
static int copy_buf_len = 0;

static int changed = 0;

GeglNode *preview_loader;

int rendered_frame = -1;
int done_frame     = -1;
static gpointer renderer_thread (gpointer data)
{
  GeglEDL *edl = data;
  while (1)
  {
    if (edl->active_source)
    {
      if (edl->source_frame_no != done_frame)
      {
        rendered_frame = edl->source_frame_no;
        gegl_node_set (preview_loader, "path", edl->active_source->path, NULL);
        gegl_node_set (preview_loader, "frame", edl->source_frame_no, NULL);
        GeglRectangle ext = gegl_node_get_bounding_box (preview_loader);
        gegl_buffer_set_extent (edl->buffer, &ext);
        gegl_node_process (edl->source_store_buf);
        done_frame = rendered_frame;
        MrgRectangle rect = {mrg_width (edl->mrg)/2, 0,
                             mrg_width (edl->mrg)/2, mrg_height (edl->mrg)/2};
        mrg_queue_draw (edl->mrg, &rect);
      }
      else
        g_usleep (100);
    }
    else
    {
      if (edl->frame_no != done_frame)
      {
        rendered_frame = edl->frame_no;
        GeglRectangle ext = gegl_node_get_bounding_box (edl->result);
        gegl_buffer_set_extent (edl->buffer, &ext);
        rig_frame (edl, edl->frame_no);
        gegl_node_process (edl->store_buf);
        done_frame = rendered_frame;
        MrgRectangle rect = {mrg_width (edl->mrg)/2, 0,
                             mrg_width (edl->mrg)/2, mrg_height (edl->mrg)/2};
        mrg_queue_draw (edl->mrg, &rect);
      }
      else
        g_usleep (100);
    }
  }
  return NULL;
}

static void mrg_gegl_blit (Mrg *mrg,
                          float x0, float y0,
                          float width, float height,
                          GeglNode *node,
                          float u, float v)
{
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

    {
      float scale = 1.0;
      scale = width / bounds.width;
      if (height / bounds.height < scale)
        scale = height / bounds.height;

      gegl_node_blit (node, scale, &roi, fmt, buf, width * 4, 
                      GEGL_BLIT_DEFAULT);
    }

  surface = cairo_image_surface_create_for_data (buf, CAIRO_FORMAT_RGB24, width, height, width * 4);
  }

  cairo_save (cr);
  cairo_surface_set_device_scale (surface, 1.0, 1.0);

  cairo_rectangle (cr, x0, y0, width, height);

  cairo_clip (cr);
  cairo_translate (cr, x0, y0);
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
  void   (*ui) (Mrg *mrg, void *state);
  Mrg     *mrg;
  GeglEDL *edl;
  GeglEDL *edl2;
  char    *path;
  char    *save_path;
};

static int playing  = 0;
float fpx           = 2;

static void *prev_sclip = NULL;

void make_active_source (GeglEDL *edl, SourceClip *clip);
void make_active_source (GeglEDL *edl, SourceClip *clip)
{
  edl->active_clip = NULL;
  edl->active_source = clip;
  edl->source_frame_no = clip->start;
  edl->selection_start = clip->start;
  edl->selection_end = clip->end;
}

static void clicked_source_clip (MrgEvent *e, void *data1, void *data2)
{
  SourceClip *clip = data1;
  GeglEDL *edl = data2;

  if (prev_sclip != clip)
  {
    make_active_source (edl, clip);
  }
  else
  {
    edl->active_clip = NULL;
    edl->active_source = clip;
    edl->source_frame_no = e->x;
    edl->selection_start = edl->frame_no;
    edl->selection_end = edl->frame_no;
  }
  mrg_queue_draw (e->mrg, NULL);
  prev_sclip = clip;
  changed++;
}

//void rig_frame (int frame_no);
static void clicked_clip (MrgEvent *e, void *data1, void *data2)
{
  Clip *clip = data1;
  GeglEDL *edl = data2;
  
  edl->frame_no = e->x;
  edl->selection_start = edl->frame_no;
  edl->selection_end = edl->frame_no;
  edl->active_clip = clip;
  edl->active_source = NULL;
  playing = 0;
  mrg_queue_draw (e->mrg, NULL);
  changed++;
}

static void drag_source_clip (MrgEvent *e, void *data1, void *data2)
{
  GeglEDL *edl = data2;
  edl->source_frame_no = e->x;
  if (e->x >= edl->selection_start)
  {
    edl->selection_end = e->x;
  }
  else
  {
    edl->selection_start = e->x;
  }
  mrg_queue_draw (e->mrg, NULL);
  changed++;
}

static void drag_clip (MrgEvent *e, void *data1, void *data2)
{
  GeglEDL *edl = data2;
  edl->frame_no = e->x;
  if (e->x >= edl->selection_start)
  {
    edl->selection_end = e->x;
  }
  else
  {
    edl->selection_start = e->x;
  }
  mrg_queue_draw (e->mrg, NULL);
  changed++;
}

static void released_source_clip (MrgEvent *e, void *data1, void *data2)
{
}

static void released_clip (MrgEvent *e, void *data1, void *data2)
{
  Clip *clip = data1;
  GeglEDL *edl = data2;
  edl->frame_no = e->x;
  edl->active_clip = clip;
  edl->active_source = NULL;
  if (edl->selection_end < edl->selection_start)
  {
    int temp = edl->selection_end;
    edl->selection_end = edl->selection_start;
    edl->selection_start = temp;
  }
  mrg_queue_draw (e->mrg, NULL);
  changed++;
}

static void stop_playing (MrgEvent *event, void *data1, void *data2)
{
  playing = 0;
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

static void toggle_playing (MrgEvent *event, void *data1, void *data2)
{
  playing =  !playing;
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

static void select_all (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  gedl_set_selection (edl, 0, gedl_get_duration (edl));
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
  changed++;
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
  changed++;
}
static void remove_clip (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (edl->active_source)
  {
    GList *iter = g_list_find (edl->clip_db, edl->active_source);
    if (iter) iter = iter->next;
    if (!iter)
      return;
    edl->clip_db = g_list_remove (edl->clip_db, edl->active_source);
    if (iter) edl->active_source = iter->data;
    else
        edl->active_source = NULL;
    edl->frame=-1;
    mrg_event_stop_propagate (event);
    mrg_queue_draw (event->mrg, NULL);
    changed++;
    return;
  }

  if (!edl->active_clip)
    return;
  {
    GList *iter = g_list_find (edl->clips, edl->active_clip);
    if (iter) iter = iter->next;
    if (!iter)
      return;
    edl->clips = g_list_remove (edl->clips, edl->active_clip);
    if (iter) edl->active_clip = iter->data;
    else
        edl->active_clip = NULL;
    frob_fade (edl->active_clip);
  }
  edl->frame=-1;
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}
static void duplicate_clip (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (edl->active_source)
  {
    GList *iter = g_list_find (edl->clip_db, edl->active_source);
    Clip *clip = clip_new_full (edl->active_source->path, edl->active_source->start, edl->active_source->end);
    edl->clips = g_list_insert_before (edl->clips, iter, clip);
    edl->active_source = (void*)clip;
    edl->frame=-1;
    mrg_event_stop_propagate (event);
    mrg_queue_draw (event->mrg, NULL);
    changed++;
    return;
  }

  if (!edl->active_clip)
    return;
  {
    GList *iter = g_list_find (edl->clips, edl->active_clip);
    Clip *clip = clip_new_full (edl->active_clip->path, edl->active_clip->start, edl->active_clip->end);
    edl->clips = g_list_insert_before (edl->clips, iter, clip);
    frob_fade (edl->active_clip);
    if (edl->active_clip->filter_graph)
      clip->filter_graph = g_strdup (edl->active_clip->filter_graph);
    edl->active_clip = clip;
    frob_fade (edl->active_clip);
  }
  edl->frame=-1;
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

static void nav_left (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (!edl->active_clip)
    return;
  {
    GList *iter = g_list_find (edl->clips, edl->active_clip);
    if (iter) iter = iter->prev;
    if (iter) edl->active_clip = iter->data;
    edl->frame_no = edl->active_clip->abs_start + 1;
  }
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

static void nav_right (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (!edl->active_clip)
    return;
  {
    GList *iter = g_list_find (edl->clips, edl->active_clip);
    if (iter) iter = iter->next;
    if (iter) edl->active_clip = iter->data;
    edl->frame_no = edl->active_clip->abs_start + 1;
  }
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

static void toggle_fade (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (!edl->active_clip)
    return;
  edl->active_clip->fade_out = !edl->active_clip->fade_out;
  frob_fade (edl->active_clip);
  {
    GList *iter = g_list_find (edl->clips, edl->active_clip);
    if (iter) iter = iter->next;
    if (iter) {
      Clip *clip2 = iter->data;
      clip2->fade_in = edl->active_clip->fade_out;
      frob_fade (clip2);
    }
  }
  if (!edl->active_clip->fade_out)
    edl->active_clip->fade_pad_end = 0;
  edl->frame = -1;
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

static void save_edl (GeglEDL *edl)
{
  if (edl->path)
  {
    gedl_save_path (edl, edl->path);
    //fprintf (stderr, "saved to %s\n", edl->path);
  }
}

static void save (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  save_edl (edl);
}

static gboolean save_idle (gpointer edl)
{
  if (changed)
  {
    changed = 0;
    save_edl (edl);
  }
  return TRUE;
}

static void set_range (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  int start, end;

  gedl_get_selection (edl, &start, &end);
  gedl_set_range (edl, start, end);
  mrg_queue_draw (event->mrg, NULL);
}

static Clip * edl_get_clip_for_frame (GeglEDL *edl, int frame);


static void up (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (edl->active_source)
  {
    GList *l;
    int found = 0;
    for (l = edl->clip_db; l; l = l->next)
    {
      if (l->next && l->next->data == edl->active_source)
      {
        edl->active_source = l->data;
        make_active_source (edl, edl->active_source);
        found = 1;
        break;
      }
    }
    if (!found)
    {
      edl->active_source = NULL;
      edl->active_clip = edl_get_clip_for_frame (edl, edl->frame_no);
    }
  }
  else
  {
    edl->active_clip = NULL;
    edl->active_source = edl->clip_db?g_list_last (edl->clip_db)->data:NULL;
    make_active_source (edl, edl->active_source);

  }
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

static void down (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (edl->active_source)
  {
    GList *l;
    int found = 0;
    for (l = edl->clip_db; l; l = l->next)
    {
      if (l->next && l->data == edl->active_source)
      {
        edl->active_source = l->next->data;
        make_active_source (edl, l->next->data);
        found = 1;
        break;
      }
    }
    if (!found)
    {
      edl->active_clip = edl_get_clip_for_frame (edl, edl->frame_no);
      edl->active_source = NULL;
    }
  }
  else
  {
    edl->active_source = edl->clip_db->data;
    make_active_source (edl, (void*)edl->active_source);
    edl->active_clip = NULL;
  }
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

static void step_frame_back (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  stop_playing (event, data1, data2);
  edl->frame_no --;
  edl->active_clip = edl_get_clip_for_frame (edl, edl->frame_no);
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

static void step_frame (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  stop_playing (event, data1, data2);
  edl->frame_no ++;
  edl->active_clip = edl_get_clip_for_frame (edl, edl->frame_no);
  mrg_event_stop_propagate (event);
  mrg_queue_draw (event->mrg, NULL);
  changed++;
}

static void clip_end_inc (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (edl->active_clip)
    {
      edl->active_clip->end++;
      edl->frame=-1;
      mrg_event_stop_propagate (event);
      mrg_queue_draw (event->mrg, NULL);
      changed++;
    }
}

static void clip_end_dec (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (edl->active_clip)
    {
      edl->active_clip->end--;
      edl->frame=-1;
      mrg_event_stop_propagate (event);
      mrg_queue_draw (event->mrg, NULL);
      changed++;
    }
}

static void clip_start_inc (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (edl->active_clip)
    {
      edl->active_clip->start++;
      edl->frame=-1; // hack - to dirty it
      mrg_event_stop_propagate (event);
      mrg_queue_draw (event->mrg, NULL);
      changed++;
    }
}

static void clip_start_dec (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  if (edl->active_clip)
    {
      edl->active_clip->start--;
      edl->frame=-1;
      mrg_event_stop_propagate (event);
      mrg_queue_draw (event->mrg, NULL);
      changed++;
    }
}

static void do_quit (MrgEvent *event, void *data1, void *data2)
{
  mrg_quit (event->mrg);
}

long last_frame = 0;

static Clip * edl_get_clip_for_frame (GeglEDL *edl, int frame)
{
  GList *l;
  int t = 0;
  for (l = edl->clips; l; l = l->next)
  {
    Clip *clip = l->data;
    if (frame >= t && frame < t + clip_get_frames (clip))
    {
      return clip;
    }
    t += clip_get_frames (clip);
  }

  return NULL;
}

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

static void zoom_timeline (MrgEvent *event, void *data1, void *data2)
{
  GeglEDL *edl = data1;
  switch (event->scroll_direction)
  {
    case MRG_SCROLL_DIRECTION_UP:
      edl->scale *= 1.02;
      break;
    case MRG_SCROLL_DIRECTION_DOWN:
      edl->scale /= 1.02;
      break;
    case MRG_SCROLL_DIRECTION_LEFT:
      edl->t0 += 2;
      break;
    case MRG_SCROLL_DIRECTION_RIGHT:
      edl->t0 -= 2;
      break;
  }
  mrg_queue_draw (event->mrg, NULL);
}

#define VID_HEIGHT 40
#define PAD_DIM     8

void render_clip (Mrg *mrg, const char *clip_path, int clip_start, int clip_frames, double x, double y)
{
  char thumb_path[PATH_MAX];
  sprintf (thumb_path, "%s.png", clip_path); /* XXX: replace with function */
  cairo_t *cr = mrg_cr (mrg);
  cairo_rectangle (cr, x, y, clip_frames, VID_HEIGHT);
  cairo_set_source_rgba (cr, 0.1, 0.1, 0.1, 0.5);

  int width, height;
  MrgImage *img = mrg_query_image (mrg, thumb_path, &width, &height);
  if (img && width > 0)
  {
    cairo_surface_t *surface = mrg_image_get_surface (img);
    cairo_matrix_t   matrix;
    cairo_pattern_t *pattern = cairo_pattern_create_for_surface (surface);
    cairo_matrix_init_scale (&matrix, 1.0, height * 1.0/ VID_HEIGHT);
    cairo_matrix_translate  (&matrix, -(x - clip_start), -y);
    cairo_pattern_set_matrix (pattern, &matrix);
    cairo_pattern_set_filter (pattern, CAIRO_FILTER_NEAREST);
    cairo_set_source (cr, pattern);

    cairo_save (cr);
    cairo_clip_preserve (cr);
    cairo_paint   (cr);
    cairo_restore (cr);
  }
  else
  {
    cairo_fill_preserve (cr);
  }
}

void gedl_draw (Mrg     *mrg,
                GeglEDL *edl,
                double   x0,
                double    y,
                double  fpx,
                double   t0)
{

  GList *l;
  cairo_t *cr = mrg_cr (mrg);
  double t;
 
  t = 0;

  cairo_set_source_rgba (cr, 1, 1,1, 1);
  cairo_set_font_size (cr, 10.0);
  y += PAD_DIM * 2;
  cairo_move_to (cr, x0 + PAD_DIM, y + VID_HEIGHT + PAD_DIM * 3);
  //cairo_show_text (cr, edl->path);
  cairo_save (cr);
  cairo_translate (cr,  x0, 0);
  cairo_scale (cr, 1.0/fpx, 1);
  cairo_translate (cr, -t0, 0);

  int start = 0, end = 0;
  gedl_get_selection (edl, &start, &end);

  cairo_rectangle (cr, start + 0.5, y - PAD_DIM, end - start, VID_HEIGHT + PAD_DIM * 2);
  cairo_set_source_rgba (cr, 1, 0, 0, 0.5);
  cairo_fill (cr);

  for (l = edl->clips; l; l = l->next)
  {
    Clip *clip = l->data;

    render_clip (mrg, clip->path, clip->start, clip_get_frames (clip), t, y);
    if (clip == edl->active_clip)
      cairo_set_source_rgba (cr, 1, 1, 0.5, 1.0);
    else
      cairo_set_source_rgba (cr, 1, 1, 1, 0.5);
    mrg_listen (mrg, MRG_PRESS, clicked_clip, clip, edl);
    mrg_listen (mrg, MRG_DRAG, drag_clip, clip, edl);
    mrg_listen (mrg, MRG_RELEASE, released_clip, clip, edl);
    cairo_stroke (cr);

    t += clip_get_frames (clip);
  }


  gedl_get_range (edl, &start, &end);

  cairo_rectangle (cr, start, y + VID_HEIGHT + PAD_DIM * 1.5, end - start, PAD_DIM);
  cairo_set_source_rgba (cr, 0, 0.11, 0.0, 0.5);
  cairo_fill_preserve (cr);
  cairo_set_source_rgba (cr, 1, 1, 1, 0.5);
  cairo_stroke (cr);

  double frame = edl->frame_no;
  if (fpx < 1.0)
    cairo_rectangle (cr, frame, y-PAD_DIM, 1.0, VID_HEIGHT + PAD_DIM * 2);
  else
    cairo_rectangle (cr, frame, y-PAD_DIM, fpx, VID_HEIGHT + PAD_DIM * 2);
  cairo_set_source_rgba (cr,1,0,0,1);
  cairo_fill (cr);

  cairo_restore (cr);

  cairo_rectangle (cr, 0, y - PAD_DIM, mrg_width (mrg), VID_HEIGHT + PAD_DIM * 4);
  mrg_listen (mrg, MRG_SCROLL, zoom_timeline, edl, NULL);
  cairo_new_path (cr);
}

static const char *css =
" document { background: black; }"
"";

void render_clip2 (Mrg *mrg, GeglEDL *edl, SourceClip *clip, float x, float y, float w, float h)
{
        cairo_t *cr = mrg_cr (mrg);
    if (clip->duration == 0)
       {
	     GeglNode *gegl = gegl_node_new ();
	     GeglNode *probe = gegl_node_new_child (gegl, "operation",
                          "gegl:ff-load", "path", clip->path, NULL);
	     gegl_node_process (probe);
	     gegl_node_get (probe, "frames", &clip->duration, NULL);
         g_object_unref (gegl);
       }

    cairo_save (cr);
    {
      float scale = 1.0;
      if (clip->duration > w)
        scale = w / clip->duration;
      cairo_scale (cr, scale, 1);

      render_clip (mrg, clip->path, 0, clip->duration, 0, y);

    if (clip == (void*)edl->active_source)
    {
      cairo_set_source_rgba (cr, 1, 1, 0.5, 1.0);
      cairo_stroke_preserve (cr);
    }
    else
    {
      cairo_set_source_rgba (cr, 1, 1, 1, 0.5);
    }

    mrg_listen (mrg, MRG_PRESS, clicked_source_clip, clip, edl);
    mrg_listen (mrg, MRG_DRAG, drag_source_clip, clip, edl);
    mrg_listen (mrg, MRG_DRAG, released_source_clip, clip, edl);
    cairo_new_path (cr);

    cairo_set_source_rgba (cr, 0,0,0,0.75);
    cairo_rectangle (cr, 0, y, clip->start, VID_HEIGHT);
    cairo_rectangle (cr, clip->end, y, clip->duration - clip->end, VID_HEIGHT);
    cairo_fill (cr);

    if (clip == (void*)edl->active_source)
      {
        double frame = edl->source_frame_no;
        if (scale > 1.0)
          cairo_rectangle (cr, frame, y-PAD_DIM, 1.0, VID_HEIGHT + PAD_DIM * 2);
        else
          cairo_rectangle (cr, frame, y-PAD_DIM, 1.0/scale, VID_HEIGHT + PAD_DIM * 2);
        cairo_set_source_rgba (cr,1,0,0,1);
        cairo_fill (cr);


  int start = 0, end = 0;
  gedl_get_selection (edl, &start, &end);

  cairo_rectangle (cr, start + 0.5, y - PAD_DIM, end - start, VID_HEIGHT + PAD_DIM * 2);
  cairo_set_source_rgba (cr, 1, 0, 0, 0.5);
  cairo_fill (cr);

      }
      
    cairo_restore (cr);
    if (0) /* draw more aspect right selection (at least for start/end "frame aspect part..?) */
    {
    render_clip (mrg, clip->path, clip->start, (clip->end - clip->start) * scale, clip->start * scale, y);
    cairo_new_path (cr);
    }
    }
}

void draw_clips (Mrg *mrg, GeglEDL *edl, float x, float y, float w, float h)
{
  GList *l;

  for (l = edl->clip_db; l; l = l->next)
  {
    SourceClip *clip = l->data;
    render_clip2 (mrg, edl, clip, x, y, w, h);

    y += VID_HEIGHT + PAD_DIM * 1;
  }
#if 0
  if (edl->active_clip)
    render_clip2 (mrg, edl, (void*)edl->active_clip, x, y, w, h);
#endif
}

void playing_iteration (Mrg *mrg, GeglEDL *edl)
{
  if (playing)
    {
      if (rendered_frame != done_frame)
        return;
      if (edl->active_source)
      {
        edl->source_frame_no++;
        if (edl->source_frame_no > edl->active_source->end)
        {
           edl->source_frame_no = 0;
           edl->source_frame_no = edl->active_source->start;
        }
      }

      if (edl->active_clip)
      {
        edl->frame_no++;
        int start, end;
        gedl_get_range (edl, &start, &end);
        if (edl->frame_no > max_frame (edl))
        {
           edl->frame_no = 0;
           if (end)
             edl->frame_no = start;
        }
        edl->active_clip = edl_get_clip_for_frame (edl, edl->frame_no);
      }
      mrg_queue_draw (mrg, NULL);
    }
}

void gedl_ui (Mrg *mrg, void *data)
{
  State *o = data;
  GeglEDL *edl = o->edl;

  mrg_stylesheet_add (mrg, css, NULL, 0, NULL);
  cairo_set_source_rgb (mrg_cr (mrg), 0,0,0);
  cairo_paint (mrg_cr (mrg));

  /* draw source clip list */
  draw_clips (mrg, edl, 10, 40, mrg_width(mrg)/2 - 20, mrg_height(mrg)/2 - 30);

#if 0
  if (edl->active_source)
  {
    gegl_node_set (preview_loader, 
            "path", edl->active_source->path, 
            "frame", edl->source_frame_no, NULL);

    mrg_gegl_blit (mrg, mrg_width (mrg)/2, 0,
                        mrg_width (mrg)/2, mrg_height (mrg)/2,
                        preview_loader, 0,0);
  }
  else
  {

  /* render viewport */
  //gegl_node_process (o->edl->store_buf);
#endif

  mrg_gegl_blit (mrg, mrg_width (mrg)/2, 0,
                      mrg_width (mrg)/2, mrg_height (mrg)/2,
                      o->edl->cached_result, 0,0);
  //}
#if 0
  o->edl2->frame_no = o->edl->frame_no + 20;
  rig_frame (o->edl2, o->edl2->frame_no);
  mrg_gegl_blit (mrg, 0, 0,
                      mrg_width (mrg)/2, mrg_height (mrg)/2,
                      o->edl2->result, 0,0);
#endif

  /* draw timeline */
  gedl_draw (mrg, edl, mrg_width(mrg)/2, mrg_height (mrg)/2, edl->scale, edl->t0);

  if(0){
    cairo_t *cr = mrg_cr (mrg);
    cairo_new_path (cr);
    cairo_move_to (cr, mrg_width (mrg)/2, 0);
    cairo_line_to (cr, mrg_width (mrg)/2, mrg_height (mrg)/2);
    cairo_move_to (cr, 0, mrg_height (mrg)/2);
    cairo_line_to (cr, mrg_width (mrg), mrg_height (mrg)/2);
    cairo_set_source_rgba (cr, 1,1,1,0.5);
    cairo_stroke (cr);
  }

  mrg_set_xy (mrg, 0, mrg_height (mrg) / 2 + VID_HEIGHT + PAD_DIM * 10);

  mrg_printf (mrg, "%i\n", edl->frame_no);

  if (edl->active_clip)
    {
      mrg_printf (mrg, "%s %i %i%s", edl->active_clip->path,
                                     edl->active_clip->start, edl->active_clip->end,
                                     edl->active_clip->fade_out?" [fade]":"");

      if (edl->active_clip->filter_graph)
        mrg_printf (mrg, " %s", edl->active_clip->filter_graph);
    }

  playing_iteration (mrg, edl);

  mrg_add_binding (mrg, "delete", NULL, NULL, remove_clip, edl);
  mrg_add_binding (mrg, "control-x", NULL, NULL, remove_clip, edl);
  mrg_add_binding (mrg, "control-d", NULL, NULL, duplicate_clip, edl);
  mrg_add_binding (mrg, "space", NULL, NULL, toggle_playing, edl);
  mrg_add_binding (mrg, "control-left", NULL, NULL, nav_left, edl);
  mrg_add_binding (mrg, "control-right", NULL, NULL, nav_right, edl);
  mrg_add_binding (mrg, ".", NULL, NULL, clip_end_inc, edl);
  mrg_add_binding (mrg, "f", NULL, NULL, toggle_fade, edl);
  mrg_add_binding (mrg, "s", NULL, NULL, save, edl);
  mrg_add_binding (mrg, "control-a", NULL, NULL, select_all, edl);
  mrg_add_binding (mrg, "r", NULL, NULL, set_range, edl);
  mrg_add_binding (mrg, ",", NULL, NULL, clip_end_dec, edl);
  mrg_add_binding (mrg, "k", NULL, NULL, clip_start_inc, edl);
  mrg_add_binding (mrg, "l", NULL, NULL, clip_start_dec, edl);
  mrg_add_binding (mrg, "q", NULL, NULL, (void*)do_quit, mrg);
  mrg_add_binding (mrg, "shift-right", NULL, NULL, extend_right, edl);
  mrg_add_binding (mrg, "shift-left", NULL, NULL, extend_left, edl);
  mrg_add_binding (mrg, "right", NULL, NULL, step_frame, edl);
  mrg_add_binding (mrg, "left", NULL, NULL, step_frame_back, edl);
  mrg_add_binding (mrg, "up", NULL, NULL, up, edl);
  mrg_add_binding (mrg, "down", NULL, NULL, down, edl);
}

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
      g_usleep (100);
    }
  }
  return NULL;
}


int gedl_ui_main (GeglEDL *edl, GeglEDL *edl2);
int gedl_ui_main (GeglEDL *edl, GeglEDL *edl2)
{
  Mrg *mrg = mrg_new (800, 600, NULL);
  //Mrg *mrg = mrg_new (-1, -1, NULL);
  State o = {NULL,};
  o.mrg = mrg;
  o.edl = edl;
  o.edl2 = edl2;

  edl->mrg = mrg;
  if (edl2) edl2->mrg = mrg;
  preview_loader = gegl_node_new_child (edl->gegl, "operation", "gegl:ff-load",
                         "path", "/tmp", NULL);
  gegl_node_connect_to (preview_loader, "output", edl->source_store_buf, "input");

  edl->cache_flags = CACHE_TRY_ALL | CACHE_MAKE_ALL;
  renderer_set_range (0, 50);
  mrg_set_ui (mrg, gedl_ui, &o);
  g_timeout_add (1000, save_idle, edl);
  if (1)
    thread = g_thread_new ("renderer", renderer_thread, edl);

  gedl_get_duration (edl);

  mrg_main (mrg);
  gegl_exit ();

  return 0;
}
