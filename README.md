# gedl

## GEGL Edit Decision List

GEGL is a video editing engine using GEGL. It uses video frame input and output
sources in GEGL that decode and encode video frames along with a
commandline/oneliner friendly sparse serialization format. The same
serialization format that can be used with the gegl binary to do commandline
image processing.

At the moment there is two interfaces to gedl, the text-file format, and gcut a
microraptor gui interface. There are plans for forking gcut with a partial lua
rewrite and maintaining both front ends for a while.

![screenshot](http://pippin.gimp.org/gedl/gedl-help.png)

Creating this experiment and improving the underlying technologies to make it
possible, was made possible through community sponsorship via
https://patreon.com/pippin

### Features

-single-track editing, with cross fades
-tuning of in/out points.
-video audio correctly cut
-shuffling of clips
-background render processes rendering target-resolution cached frames.
-proxy media support, permits editing with scaled down files for interactive speeds when editing workstation cannot deal with fullhd / 4k source footage at interactive speeds.
-ui preview renderer using proxies - in separate thread for ui drawing/interaction; to keep it from blocking interaction.
-drag and drop of media from desktop file managers
-editing filter op chains per clip
-editing frame-source chains for clips (media files like images and video get implicit graphs)
-tuning scalar/string/boolean properties of ops
-animating scalar properties of ops (only linear interpolation/key-framing for
now)
-timestamped auto-save

### UI hints

The current UI is the minimal amount of UI needed for keyboard centered editing
with some mouse based scrubbing and positioning. The available keyboard actions
are different for the first and last frames of a clip compared with the
mid-clip frames, when jumping between clips with up/down arrows, one jumps
between the first frames of clips.

### Example output

The GEGL video from Libre Graphics Meeting 2016 in London,
https://www.youtube.com/watch?v=GJJPgLGrSgc was made from raw footage using
gedl, the default testproject of gedl which is in this repo as default.edl
produces the following video when rendered the first time (TODO: update this
video with newer render):
https://www.youtube.com/watch?v=n91QbTMawuc

### Dependencies:

   gegl-0.3.16  http://gegl.org/
   mrg          https://github.com/hodefoting/mrg/
   SDL-1.2
   ffmpeg

### File format tutorial

An example gedl edl file is as follows:

    output-path=result.mp4

    A.mp4 200 341
    A.mp4 514 732
    B.mp4 45 123

If this was stored in a file, test.edl we can run:

    $ gedl test.edl render

And gedl will put video and audio content belonging to times from frame no 200 to frame no 341, followed by frames from not 514 to 732 subsequently followed with frames 45-123 from another file B.mp4

if you just run:

    $ gedl test.edl

gedl will launch in UI mode, videos can be added by drag and drop from
file manager if starting out from scratch.

when quitting gedl will have overwritten the original file
with something like the following:

    output-path=example-output.mp4
    video-width=1920
    video-height=1080
    fps=59.940060
    frame-scale=1.293607
    t0=0.000000
    frame-no=311
    selection-start=216
    selection-end=216
    
    A.mp4 200 341
    A.mp4 514 732
    B.mp4 45 123

The output settings  for video-width, video-height and fps have been detected
from the first video clip - gedl works well if all clips have the same fps.

One way to speed up gedl ui editing is to set a low resolution and the key
use-proxies to 1, then gedl will generate thumbnail videos - this is a useful
way to bring the video resolution down termporarily to do the rough edit before
tuning full resolution (you can also toggle between these by entering the
resolutions). (todo: configure a preview resolution as well as a target
resolution - and toggle which of them is active - with proxy generation only
for preview resolution.. and slave render / caching to target resolution.

After each clip a gegl image processing chain following the format documented
at http://gegl.org/gegl-chain.html

One can for instance write:

    A.mp4 200 341 -- gaussian-blur std-dev-x=0.1rel std-dev-y=0.1rel gegl:threshold value=0.3

To threshold the clip, this feature can be used for using arbitrary GEGL
pipelines with interpolated parameters as filters on a video clip. The suffix
rel used in the gaussian blur is dependant on the height of the video - this
permits the pipeline to be used for proxies as well as for full size video.

Values can also be keyframed by supplying them inside inside curly brackets,
containg keyframe=value pairs in a clip local interpolated time space {0=3.0
3=0.2 10=}.

### caching architecture

The core of gedls architecture is the data storage model, the text file that
the user sees as a project file contains - together with the referenced source
assets, the complete description of how to generate a video for a sequence.

This file is broken down into a set of global assignments of key/values, and
lines describing clips with path, in/out point and associated GEGL filter
stacks.

GEDL keeps cached data in the .gedl subdir in the same directory as the loaded
GEDL project file, All the projects in a folder share the same .cache
directory. The cache data is separated in subdirs for ease of development and
debugging.

**.gedl/cache/**   contains the rendered frames - stored in files that are a hash
of a string containing , source clip/frame no and filter chain at given frame.
Thus making returns to previous settings reuse previous values.

**.gedl/history/**  contains undo snapshots of files being edited (backups from
frequent auto-save)

**.gedl/proxy/**  contains scaled down to for quick preview/editing video files

**.gedl/thumb/**  contains thumb tracks for video clips - thumb tracks are images
to show in the clips in the timeline, the thumb tracks are created using
iconographer from the proxy videos - from original would be possible, but take
longer than creating proxy videos.

when the UI is running the following threads and processes exist:

    gedl project.edl   mrg ui thread (cairo + gtk/raw fb)
                       GEGL renderer/evaluation thread

    gedl project.edl cache 0 4  background frame cache renderer processes
    gedl project.edl cache 1 4  if frameno % 4 == 1 then this one considers
    gedl project.edl cache 2 4  it its responsibility to render frameno, the
    gedl project.edl cache 3 4  count of such processes is set to the number of
                                cores/processors available.

The background renderer processes are stopped when playback is initiated, as
well as every 60 seconds, when a new set of caches (restarts to handle both
project file changes and possible memory leaks.)

