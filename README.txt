gedl
----
GEGL Edit Decision List

A simple commandline and start of ui frontend for video processing with GEGL.
The gegl binary shipped with GEGL can process a single video through one chain
of operations. This is an experimental editable text-file driven project that
can assemble segements of multiple such processed clips to one bigger video.

The GEGL video from Libre Graphics Meeting 2016 in London,
https://www.youtube.com/watch?v=GJJPgLGrSgc was made from raw footage using
gedl.

This project is shared in the public interest, both as a reference for people
to study as well as backup, it is a rough draft of a video editor - and usage
instructions for the very brave are for now: may the source be with you..

For further possible further development - processing should be split out of
the ui/editing process, and rather be done by slave process(es) that kan be
killed and spun up again, making it possible to have crashes or memory leaks in
processing be separated from the ui process.
