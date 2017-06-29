# gedl

## GEGL Edit Decision List

A simple commandline and start of ui frontend for video processing with GEGL.
The gegl binary shipped with GEGL can process a single video through one chain
of operations. This is an experimental editable text-file driven project that
can assemble segements of multiple such processed clips to one bigger video.

The GEGL video from Libre Graphics Meeting 2016 in London,
https://www.youtube.com/watch?v=GJJPgLGrSgc was made from raw footage using
gedl.

This project is shared in the public interest, both as a reference for people
to study as well as backup, it is the core of a  GEGL based video editor.

For further possible further development - processing should be split out of
the ui/editing process, and rather be done by slave process(es) that kan be
killed and spun up again, making it possible to have crashes or memory leaks in
processing be separated from the ui process.

### Dependencies:

gegl-0.3.16  http://gegl.org/
mrg          https://github.com/hodefoting/mrg/
SDL-1.2
ffmpeg


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

=============[ test.edl ]==================

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

============================================

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

with the addition that values can be keyframed when used inside curly brackets,
containe keyframe=value pairs in a clip local interpolated time space {0=3.0
3=0.2 10=}.


=====================================
Recognized global key/value pairs:

use-proxies integer - generate and use prescaled video files in ui
output-path string - the file to write the final rendered video to
videoc-codec - video codec to use - or autodetect based on extension of
               output-path
audio-codec - audio codec to use - or autodetect based on extension of
              output-path
video-width - the width of the generated video, in pixels
video-height - the height of the generated video, in pixels
video-bufsize - the buffer size to use
video-tolerance -
fps - target framerate
audio-bit-rate - bit rate to use for encoded audio

selection-start
selection-end
range-start
range-end
t0
frame-scale
frame-no

-------------------------------------

gedl keeps temporary files in the .gedl folder this folder contains subdirs
with cached frames, proxy videos and thumbtrack images. To save disk space this
folder can be removed and it will be regenerated, for now gedl works best if
launched from the folder containing the edl file.


