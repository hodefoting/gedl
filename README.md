# gedl

## GEGL Edit Decision List

gedl is a video editing engine using GEGL. It uses video frame input and output
sources in GEGL that decode and encode video frames along with a
commandline/oneliner friendly sparse serialization format. The same
serialization format that can be used with the gegl binary to do commandline
image processing.

### Features

 - single-track editing, with cross fades
 - tuning of in/out points.
 - video audio correctly cut
 - shuffling of clips
 - background render processes rendering target-resolution cached frames.
 - proxy media support, permits editing with scaled down files for interactive speeds when editing workstation cannot deal with fullhd / 4k source footage at interactive speeds.
 - ui preview renderer using proxies - in separate thread for ui drawing/interaction; to keep it from blocking interaction.
 - drag and drop of media from desktop file managers
 - editing filter op chains per clip
 - editing frame-source chains for clips (media files like images and video get implicit graphs)
 - tuning scalar/string/boolean properties of ops
 - animating scalar properties of ops (only linear interpolation/key-framing for
now)
 - timestamped auto-save

### Screenshots

The basic view, with the F1 keyboard shortcut help overlaid over the video.

![screenshot](http://pippin.gimp.org/gedl/gedl-help.png)

In this screenshot, showing the purely synthetic used gegl operations default
project of gedl. Showing visualization of keyframed parameters and permit
setting them through sliders. Slanted clip transitions indicates cross-fades.
Note that the current UI is the first attempt at a direct mapping of the file
format.

![screenshot](http://pippin.gimp.org/gedl/gedl2.png)


### Example output

The GEGL video from Libre Graphics Meeting 2016 in London,
https://www.youtube.com/watch?v=GJJPgLGrSgc was made from raw footage using
gedl, the default testproject of gedl which is in this repo as default.edl
produces the following video when rendered the first time (TODO: update this
video with newer render):
https://www.youtube.com/watch?v=n91QbTMawuc


### Development plans

The UI is written using microraptor gui, which means that things that can be
done with simple HTML+CSS or drawn using cairo when programming can be made
interactive in the local coordiantes drawn with for callbacks. 

Elements acting on top of the single-track timeline, possibly covering all the
timeline or just some clips. For filtering/replacing/overlaying video/audio.
This working similar to annotations/comments, that float with clips as
preceding clips in the timeline change duration / position. This should expand
the possible feature scope to video picture in picture, global color / mood
adjustments - audio bleeping, mixing in music - and probably more.

See [gedl.h](https://github.com/hodefoting/gedl/blob/master/gedl.h) for more
up-to date plans and documentation of at least some of the known issues.

The amount of code, written in C is about equally divided 50/50 between the
core rendering logic in C and the UI, both about 3000 lines. All the actual
work and flexibility is provided by GEGL itself, new operations become
automatically available in both GIMP and gedl when they are added to GEGL the
system.  Two ops in particular in GEGL which trace their history back to a
pre-GEGL 2004era video editor called bauxite, gegl:ff-load and gegl:ff-save,
which provide the ability to load and save a specific video frame, and
associated audio, for video files. It could be possible to add alternatives to
these operations using for instance gstreamer, and the rest of gedl would
remain unchanged.

It is planned to rewrite the UI part from C to lua, even if even this C ui is
very young, it will however use the same toolkit, cairo and microraptor gui,
aiming for shorter interaction cycles during development and less fragile code
and opening up for easier outside contributions, as well as learning from
mistakes and short comings in the UI prototype proof of concept written in C,
with mrg and cairo, anything that can be drawn can be made interactive, thus at
least editing animation curves could be made a lot more visual and interactive.

### UI hints

The current UI is the minimal amount of UI needed for keyboard centered editing
with some mouse based scrubbing and positioning. The available keyboard actions
are different for the first and last frames of a clip compared with the
mid-clip frames, when jumping between clips with up/down arrows, one jumps
between the first frames of clips.


### Dependencies:

   gegl-0.3.16  http://gegl.org/
   mrg          https://github.com/hodefoting/mrg/
   SDL-1.2
   ffmpeg

### File format

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

After each clip a gegl image processing chain following the format documented
at http://gegl.org/gegl-chain.html

One can for instance write:

    A.mp4 200 341 -- gaussian-blur std-dev-x=0.1rel std-dev-y=0.1rel gegl:threshold value=0.3

To threshold the clip, this feature can be used for using arbitrary GEGL
pipelines with interpolated parameters as filters on a video clip. The suffix
rel used in the gaussian blur is dependant on the height of the video - this
permits the pipeline to be used for proxies as well as for full size video.

Values can also be animated by supplying them inside inside curly brackets,
containg keyframe=value pairs in a clip local interpolated time space {0=3.0
3=0.2 10=}. The format for the animated properties are likely to change as the
current place-holder linear only format is supplanted.

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

